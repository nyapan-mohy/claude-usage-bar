// OAuth 2.0 PKCE flow, credential storage, and token management.
// Ported from macOS: UsageService.swift (OAuth parts), StoredCredentials.swift

#include "oauth.h"
#include "config.h"
#include "string_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>

namespace oauth {

// ============================================================
// Internal helpers
// ============================================================

namespace {

// ISO 8601 format with 'Z' suffix for UTC
std::string time_point_to_iso8601(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    struct tm utc_tm {};
    gmtime_s(&utc_tm, &time_t_val);

    // Get sub-second fractional part
    auto since_epoch = tp.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << millis;
    oss << 'Z';
    return oss.str();
}

std::optional<std::chrono::system_clock::time_point> iso8601_to_time_point(const std::string& str) {
    // Parse "2025-01-15T12:30:00.000Z" or "2025-01-15T12:30:00Z"
    struct tm utc_tm {};
    int millis = 0;

    std::istringstream iss(str);
    iss >> std::get_time(&utc_tm, "%Y-%m-%dT%H:%M:%S");
    if (iss.fail()) return std::nullopt;

    // Try to parse fractional seconds
    if (iss.peek() == '.') {
        char dot;
        iss >> dot;
        std::string frac;
        while (iss.peek() != 'Z' && iss.peek() != '+' && iss.peek() != '-' && !iss.eof()) {
            char c;
            iss >> c;
            frac += c;
        }
        if (!frac.empty()) {
            // Pad or truncate to 3 digits
            while (frac.size() < 3) frac += '0';
            millis = std::stoi(frac.substr(0, 3));
        }
    }

    // Convert to time_t (interpret as UTC)
    auto time_t_val = _mkgmtime(&utc_tm);
    if (time_t_val == static_cast<time_t>(-1)) return std::nullopt;

    auto tp = std::chrono::system_clock::from_time_t(time_t_val);
    tp += std::chrono::milliseconds(millis);
    return tp;
}

std::string url_encode(const std::string& value) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;
    for (auto c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::setw(2)
                    << std::uppercase
                    << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return encoded.str();
}

}  // anonymous namespace

// ============================================================
// Base64url encoding
// ============================================================

std::string base64url_encode(const uint8_t* data, size_t len) {
    // Use Windows CryptBinaryToStringA for base64 encoding
    DWORD base64_len = 0;
    CryptBinaryToStringA(
        data, static_cast<DWORD>(len),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        nullptr, &base64_len
    );

    std::string base64(base64_len, '\0');
    CryptBinaryToStringA(
        data, static_cast<DWORD>(len),
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
        base64.data(), &base64_len
    );
    base64.resize(base64_len);

    // Convert standard Base64 to Base64url: + -> -, / -> _, remove =
    for (auto& ch : base64) {
        if (ch == '+') ch = '-';
        else if (ch == '/') ch = '_';
    }
    base64.erase(std::remove(base64.begin(), base64.end(), '='), base64.end());

    return base64;
}

std::string base64url_encode(const std::vector<uint8_t>& data) {
    return base64url_encode(data.data(), data.size());
}

// ============================================================
// PKCE helpers
// ============================================================

std::string generate_code_verifier() {
    std::vector<uint8_t> bytes(config::kCodeVerifierByteCount);

    auto status = BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    if (status != 0) {
        // Fallback: should not happen in practice
        return {};
    }

    return base64url_encode(bytes);
}

std::string generate_code_challenge(const std::string& verifier) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    auto status = BCryptOpenAlgorithmProvider(
        &alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0
    );
    if (status != 0) return {};

    BCRYPT_HASH_HANDLE hash_handle = nullptr;
    status = BCryptCreateHash(alg, &hash_handle, nullptr, 0, nullptr, 0, 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    status = BCryptHashData(
        hash_handle,
        reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),
        static_cast<ULONG>(verifier.size()),
        0
    );
    if (status != 0) {
        BCryptDestroyHash(hash_handle);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    std::vector<uint8_t> hash_value(32);  // SHA-256 = 32 bytes
    status = BCryptFinishHash(
        hash_handle,
        hash_value.data(),
        static_cast<ULONG>(hash_value.size()),
        0
    );

    BCryptDestroyHash(hash_handle);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status != 0) return {};

    return base64url_encode(hash_value);
}

// ============================================================
// OAuth URL & request builders
// ============================================================

std::string build_authorize_url(
    const std::string& state,
    const std::string& code_challenge) {

    // Join scopes with space
    std::string scope_str;
    for (size_t i = 0; i < config::kDefaultOAuthScopes.size(); ++i) {
        if (i > 0) scope_str += ' ';
        scope_str += config::kDefaultOAuthScopes[i];
    }

    std::ostringstream url;
    url << config::kAuthorizeEndpoint << '?';
    url << "code=true";
    url << "&client_id=" << url_encode(config::kClientId);
    url << "&response_type=code";
    url << "&redirect_uri=" << url_encode(config::kRedirectUri);
    url << "&scope=" << url_encode(scope_str);
    url << "&code_challenge=" << url_encode(code_challenge);
    url << "&code_challenge_method=S256";
    url << "&state=" << url_encode(state);

    return url.str();
}

OAuthCodeResult parse_oauth_code(const std::string& raw_input) {
    // Trim whitespace
    auto trimmed = raw_input;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);

    auto hash_pos = trimmed.find('#');
    if (hash_pos != std::string::npos) {
        return OAuthCodeResult{
            trimmed.substr(0, hash_pos),
            trimmed.substr(hash_pos + 1)
        };
    }

    return OAuthCodeResult{trimmed, std::nullopt};
}

std::string build_token_request_body(
    const std::string& code,
    const std::string& state,
    const std::string& code_verifier) {

    nlohmann::json body;
    body["grant_type"] = "authorization_code";
    body["code"] = code;
    body["state"] = state;
    body["client_id"] = config::kClientId;
    body["redirect_uri"] = config::kRedirectUri;
    body["code_verifier"] = code_verifier;

    return body.dump();
}

std::string build_refresh_request_body(
    const std::string& refresh_token,
    const std::vector<std::string>& scopes) {

    nlohmann::json body;
    body["grant_type"] = "refresh_token";
    body["refresh_token"] = refresh_token;
    body["client_id"] = config::kClientId;

    if (!scopes.empty()) {
        std::string scope_str;
        for (size_t i = 0; i < scopes.size(); ++i) {
            if (i > 0) scope_str += ' ';
            scope_str += scopes[i];
        }
        body["scope"] = scope_str;
    }

    return body.dump();
}

// ============================================================
// Token response parsing
// ============================================================

std::optional<StoredCredentials> parse_token_response(
    const std::string& json_body,
    const StoredCredentials* fallback) {

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_body);
    } catch (...) {
        return std::nullopt;
    }

    auto it_access = j.find("access_token");
    if (it_access == j.end() || !it_access->is_string() || it_access->get<std::string>().empty()) {
        return std::nullopt;
    }

    StoredCredentials creds;
    creds.access_token = it_access->get<std::string>();

    // refresh_token: from response, or fall back
    auto it_refresh = j.find("refresh_token");
    if (it_refresh != j.end() && it_refresh->is_string()) {
        creds.refresh_token = it_refresh->get<std::string>();
    } else if (fallback) {
        creds.refresh_token = fallback->refresh_token;
    }

    // expires_in -> expires_at
    auto it_expires = j.find("expires_in");
    if (it_expires != j.end()) {
        double seconds = 0;
        if (it_expires->is_number()) {
            seconds = it_expires->get<double>();
        } else if (it_expires->is_string()) {
            try {
                seconds = std::stod(it_expires->get<std::string>());
            } catch (...) {
                seconds = 0;
            }
        }
        if (seconds > 0) {
            auto now = std::chrono::system_clock::now();
            creds.expires_at = now + std::chrono::milliseconds(
                static_cast<long long>(seconds * 1000.0));
        } else if (fallback) {
            creds.expires_at = fallback->expires_at;
        }
    } else if (fallback) {
        creds.expires_at = fallback->expires_at;
    }

    // scopes: from response, or fall back, or default
    auto it_scope = j.find("scope");
    if (it_scope != j.end() && it_scope->is_string()) {
        std::string scope_str = it_scope->get<std::string>();
        std::istringstream iss(scope_str);
        std::string token;
        while (iss >> token) {
            creds.scopes.push_back(token);
        }
    } else if (fallback) {
        creds.scopes = fallback->scopes;
    } else {
        for (const auto& s : config::kDefaultOAuthScopes) {
            creds.scopes.push_back(s);
        }
    }

    return creds;
}

// ============================================================
// StoredCredentials helpers
// ============================================================

bool needs_refresh(
    const StoredCredentials& creds,
    std::chrono::system_clock::time_point now,
    double leeway_seconds) {

    if (!has_refresh_token(creds)) return false;
    if (!creds.expires_at.has_value()) return false;

    auto leeway = std::chrono::milliseconds(
        static_cast<long long>(leeway_seconds * 1000.0));
    return creds.expires_at.value() <= (now + leeway);
}

bool has_refresh_token(const StoredCredentials& creds) {
    return creds.refresh_token.has_value() && !creds.refresh_token->empty();
}

// ============================================================
// JSON conversion
// ============================================================

void to_json(nlohmann::json& j, const StoredCredentials& v) {
    j = nlohmann::json{
        {"accessToken", v.access_token}
    };

    if (v.refresh_token.has_value()) {
        j["refreshToken"] = v.refresh_token.value();
    } else {
        j["refreshToken"] = nullptr;
    }

    if (v.expires_at.has_value()) {
        j["expiresAt"] = time_point_to_iso8601(v.expires_at.value());
    } else {
        j["expiresAt"] = nullptr;
    }

    j["scopes"] = v.scopes;
}

void from_json(const nlohmann::json& j, StoredCredentials& v) {
    j.at("accessToken").get_to(v.access_token);

    auto it_refresh = j.find("refreshToken");
    if (it_refresh != j.end() && !it_refresh->is_null()) {
        v.refresh_token = it_refresh->get<std::string>();
    } else {
        v.refresh_token = std::nullopt;
    }

    auto it_expires = j.find("expiresAt");
    if (it_expires != j.end() && !it_expires->is_null()) {
        v.expires_at = iso8601_to_time_point(it_expires->get<std::string>());
    } else {
        v.expires_at = std::nullopt;
    }

    auto it_scopes = j.find("scopes");
    if (it_scopes != j.end() && it_scopes->is_array()) {
        v.scopes = it_scopes->get<std::vector<std::string>>();
    } else {
        v.scopes.clear();
    }
}

// ============================================================
// Credential file I/O
// ============================================================

std::filesystem::path get_config_dir() {
    wchar_t app_data[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, app_data))) {
        return std::filesystem::path(app_data) / config::kConfigDirName;
    }
    // Fallback: use %USERPROFILE%/.config/
    char* user_profile = nullptr;
    size_t len = 0;
    _dupenv_s(&user_profile, &len, "USERPROFILE");
    std::string profile_str = user_profile ? user_profile : ".";
    free(user_profile);
    return std::filesystem::path(
        string_utils::utf8_to_wide(profile_str)
    ) / ".config" / config::kConfigDirName;
}

bool save_credentials(const StoredCredentials& creds) {
    return save_credentials(creds, get_config_dir());
}

bool save_credentials(const StoredCredentials& creds,
                      const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    auto creds_path = dir / "credentials.json";
    nlohmann::json j = creds;

    std::ofstream ofs(creds_path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    ofs.close();

    // Remove legacy token file if it exists
    auto legacy_path = dir / "token";
    std::filesystem::remove(legacy_path, ec);

    return true;
}

std::optional<StoredCredentials> load_credentials() {
    std::vector<std::string> default_scopes;
    for (const auto& s : config::kDefaultOAuthScopes) {
        default_scopes.push_back(s);
    }
    return load_credentials(get_config_dir(), default_scopes);
}

std::optional<StoredCredentials> load_credentials(
    const std::filesystem::path& dir,
    const std::vector<std::string>& default_scopes) {

    // Try credentials.json first
    auto creds_path = dir / "credentials.json";
    if (std::filesystem::exists(creds_path)) {
        std::ifstream ifs(creds_path);
        if (ifs.is_open()) {
            try {
                nlohmann::json j = nlohmann::json::parse(ifs);
                return j.get<StoredCredentials>();
            } catch (...) {
                // Fall through to legacy
            }
        }
    }

    // Legacy token file fallback
    auto legacy_path = dir / "token";
    if (std::filesystem::exists(legacy_path)) {
        std::ifstream ifs(legacy_path);
        if (ifs.is_open()) {
            std::string token;
            std::getline(ifs, token);
            // Trim whitespace
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            if (!token.empty()) {
                StoredCredentials creds;
                creds.access_token = token;
                creds.scopes = default_scopes;
                return creds;
            }
        }
    }

    return std::nullopt;
}

void delete_credentials() {
    delete_credentials(get_config_dir());
}

void delete_credentials(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove(dir / "credentials.json", ec);
    std::filesystem::remove(dir / "token", ec);
}

}  // namespace oauth
