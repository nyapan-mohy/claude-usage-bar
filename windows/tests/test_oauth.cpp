// Tests for OAuth PKCE flow, credential storage, and token management.

#include <gtest/gtest.h>
#include "oauth.h"
#include "config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

namespace {

// Helper: check that a string contains only base64url characters
bool is_base64url(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c))
            || c == '-' || c == '_';
    });
}

// Helper: create a temp directory for credential tests
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        path = std::filesystem::path(tmp) / "oauth_test";
        std::filesystem::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // anonymous namespace

// ============================================================
// base64url_encode
// ============================================================

TEST(Base64UrlEncode, EmptyInput) {
    std::vector<uint8_t> empty;
    auto result = oauth::base64url_encode(empty);
    EXPECT_TRUE(result.empty());
}

TEST(Base64UrlEncode, NoPadding) {
    // Any output should have no '=' characters
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = oauth::base64url_encode(data);
    EXPECT_EQ(result.find('='), std::string::npos);
}

TEST(Base64UrlEncode, NoStandardBase64Chars) {
    // '+' and '/' should be replaced with '-' and '_'
    // Feed bytes that would produce '+' or '/' in standard base64
    // 0xFB, 0xFF, 0xBF -> standard base64 = "u/+/" -> base64url = "u_-_"
    std::vector<uint8_t> data = {0xFB, 0xFF, 0xBF};
    auto result = oauth::base64url_encode(data);
    EXPECT_EQ(result.find('+'), std::string::npos);
    EXPECT_EQ(result.find('/'), std::string::npos);
    EXPECT_TRUE(is_base64url(result));
}

TEST(Base64UrlEncode, KnownValue) {
    // "Hello" -> base64 = "SGVsbG8=" -> base64url = "SGVsbG8"
    std::string input = "Hello";
    std::vector<uint8_t> data(input.begin(), input.end());
    auto result = oauth::base64url_encode(data);
    EXPECT_EQ(result, "SGVsbG8");
}

// ============================================================
// generate_code_verifier
// ============================================================

TEST(GenerateCodeVerifier, ProducesBase64UrlString) {
    auto verifier = oauth::generate_code_verifier();
    EXPECT_FALSE(verifier.empty());
    EXPECT_TRUE(is_base64url(verifier));
}

TEST(GenerateCodeVerifier, CorrectLength) {
    // 32 bytes -> 43 base64url chars (ceil(32*4/3) = 43)
    auto verifier = oauth::generate_code_verifier();
    EXPECT_EQ(verifier.size(), 43u);
}

TEST(GenerateCodeVerifier, ProducesUniqueValues) {
    auto v1 = oauth::generate_code_verifier();
    auto v2 = oauth::generate_code_verifier();
    EXPECT_NE(v1, v2);
}

// ============================================================
// generate_code_challenge
// ============================================================

TEST(GenerateCodeChallenge, ProducesBase64UrlString) {
    auto verifier = oauth::generate_code_verifier();
    auto challenge = oauth::generate_code_challenge(verifier);
    EXPECT_FALSE(challenge.empty());
    EXPECT_TRUE(is_base64url(challenge));
}

TEST(GenerateCodeChallenge, CorrectLength) {
    // SHA256 = 32 bytes -> 43 base64url chars
    auto verifier = oauth::generate_code_verifier();
    auto challenge = oauth::generate_code_challenge(verifier);
    EXPECT_EQ(challenge.size(), 43u);
}

TEST(GenerateCodeChallenge, KnownVerifierKnownChallenge) {
    // RFC 7636 Appendix B test vector (with base64url encoding):
    // verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"
    // SHA256 of that -> base64url = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    auto challenge = oauth::generate_code_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
    EXPECT_EQ(challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST(GenerateCodeChallenge, DeterministicForSameInput) {
    auto verifier = oauth::generate_code_verifier();
    auto c1 = oauth::generate_code_challenge(verifier);
    auto c2 = oauth::generate_code_challenge(verifier);
    EXPECT_EQ(c1, c2);
}

// ============================================================
// parse_oauth_code
// ============================================================

TEST(ParseOAuthCode, CodeWithState) {
    auto result = oauth::parse_oauth_code("mycode#mystate");
    EXPECT_EQ(result.code, "mycode");
    ASSERT_TRUE(result.state.has_value());
    EXPECT_EQ(result.state.value(), "mystate");
}

TEST(ParseOAuthCode, CodeWithoutState) {
    auto result = oauth::parse_oauth_code("mycode");
    EXPECT_EQ(result.code, "mycode");
    EXPECT_FALSE(result.state.has_value());
}

TEST(ParseOAuthCode, EmptyString) {
    auto result = oauth::parse_oauth_code("");
    EXPECT_TRUE(result.code.empty());
    EXPECT_FALSE(result.state.has_value());
}

TEST(ParseOAuthCode, TrimsWhitespace) {
    auto result = oauth::parse_oauth_code("  mycode#mystate  ");
    EXPECT_EQ(result.code, "mycode");
    ASSERT_TRUE(result.state.has_value());
    EXPECT_EQ(result.state.value(), "mystate");
}

TEST(ParseOAuthCode, MultipleHashSigns) {
    // Only first '#' should be used as separator
    auto result = oauth::parse_oauth_code("code#state#extra");
    EXPECT_EQ(result.code, "code");
    ASSERT_TRUE(result.state.has_value());
    EXPECT_EQ(result.state.value(), "state#extra");
}

// ============================================================
// build_authorize_url
// ============================================================

TEST(BuildAuthorizeUrl, ContainsRequiredParameters) {
    auto url = oauth::build_authorize_url("test_state", "test_challenge");

    EXPECT_NE(url.find("code=true"), std::string::npos);
    EXPECT_NE(url.find("client_id="), std::string::npos);
    EXPECT_NE(url.find("response_type=code"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri="), std::string::npos);
    EXPECT_NE(url.find("scope="), std::string::npos);
    EXPECT_NE(url.find("code_challenge=test_challenge"), std::string::npos);
    EXPECT_NE(url.find("code_challenge_method=S256"), std::string::npos);
    EXPECT_NE(url.find("state=test_state"), std::string::npos);
}

TEST(BuildAuthorizeUrl, StartsWithCorrectEndpoint) {
    auto url = oauth::build_authorize_url("s", "c");
    EXPECT_EQ(url.substr(0, strlen(config::kAuthorizeEndpoint)),
              config::kAuthorizeEndpoint);
}

// ============================================================
// build_token_request_body
// ============================================================

TEST(BuildTokenRequestBody, ContainsAllFields) {
    auto body = oauth::build_token_request_body("code123", "state456", "verifier789");
    auto j = nlohmann::json::parse(body);

    EXPECT_EQ(j["grant_type"], "authorization_code");
    EXPECT_EQ(j["code"], "code123");
    EXPECT_EQ(j["state"], "state456");
    EXPECT_EQ(j["client_id"], config::kClientId);
    EXPECT_EQ(j["redirect_uri"], config::kRedirectUri);
    EXPECT_EQ(j["code_verifier"], "verifier789");
}

// ============================================================
// build_refresh_request_body
// ============================================================

TEST(BuildRefreshRequestBody, ContainsAllFields) {
    std::vector<std::string> scopes = {"user:profile", "user:inference"};
    auto body = oauth::build_refresh_request_body("refresh_tok", scopes);
    auto j = nlohmann::json::parse(body);

    EXPECT_EQ(j["grant_type"], "refresh_token");
    EXPECT_EQ(j["refresh_token"], "refresh_tok");
    EXPECT_EQ(j["client_id"], config::kClientId);
    EXPECT_EQ(j["scope"], "user:profile user:inference");
}

TEST(BuildRefreshRequestBody, NoScopeWhenEmpty) {
    std::vector<std::string> scopes;
    auto body = oauth::build_refresh_request_body("refresh_tok", scopes);
    auto j = nlohmann::json::parse(body);

    EXPECT_FALSE(j.contains("scope"));
}

// ============================================================
// parse_token_response
// ============================================================

TEST(ParseTokenResponse, ValidResponse) {
    nlohmann::json j = {
        {"access_token", "at_123"},
        {"refresh_token", "rt_456"},
        {"expires_in", 3600},
        {"scope", "user:profile user:inference"}
    };

    auto result = oauth::parse_token_response(j.dump());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->access_token, "at_123");
    ASSERT_TRUE(result->refresh_token.has_value());
    EXPECT_EQ(result->refresh_token.value(), "rt_456");
    ASSERT_TRUE(result->expires_at.has_value());
    EXPECT_EQ(result->scopes.size(), 2u);
    EXPECT_EQ(result->scopes[0], "user:profile");
    EXPECT_EQ(result->scopes[1], "user:inference");
}

TEST(ParseTokenResponse, ExpiresInAsDouble) {
    nlohmann::json j = {
        {"access_token", "at"},
        {"expires_in", 3600.5}
    };

    auto result = oauth::parse_token_response(j.dump());
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->expires_at.has_value());
}

TEST(ParseTokenResponse, ExpiresInAsString) {
    nlohmann::json j = {
        {"access_token", "at"},
        {"expires_in", "3600"}
    };

    auto result = oauth::parse_token_response(j.dump());
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->expires_at.has_value());
}

TEST(ParseTokenResponse, NoAccessTokenReturnsNullopt) {
    nlohmann::json j = {
        {"refresh_token", "rt_456"}
    };

    auto result = oauth::parse_token_response(j.dump());
    EXPECT_FALSE(result.has_value());
}

TEST(ParseTokenResponse, EmptyAccessTokenReturnsNullopt) {
    nlohmann::json j = {
        {"access_token", ""}
    };

    auto result = oauth::parse_token_response(j.dump());
    EXPECT_FALSE(result.has_value());
}

TEST(ParseTokenResponse, InvalidJsonReturnsNullopt) {
    auto result = oauth::parse_token_response("not json at all");
    EXPECT_FALSE(result.has_value());
}

TEST(ParseTokenResponse, FallbackInheritsRefreshToken) {
    oauth::StoredCredentials fallback;
    fallback.access_token = "old_at";
    fallback.refresh_token = "old_rt";
    fallback.scopes = {"scope1"};

    nlohmann::json j = {
        {"access_token", "new_at"}
    };

    auto result = oauth::parse_token_response(j.dump(), &fallback);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->access_token, "new_at");
    ASSERT_TRUE(result->refresh_token.has_value());
    EXPECT_EQ(result->refresh_token.value(), "old_rt");
    EXPECT_EQ(result->scopes, fallback.scopes);
}

TEST(ParseTokenResponse, ResponseOverridesFallback) {
    oauth::StoredCredentials fallback;
    fallback.access_token = "old_at";
    fallback.refresh_token = "old_rt";
    fallback.scopes = {"scope1"};

    nlohmann::json j = {
        {"access_token", "new_at"},
        {"refresh_token", "new_rt"},
        {"scope", "new_scope"}
    };

    auto result = oauth::parse_token_response(j.dump(), &fallback);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->refresh_token.value(), "new_rt");
    ASSERT_EQ(result->scopes.size(), 1u);
    EXPECT_EQ(result->scopes[0], "new_scope");
}

TEST(ParseTokenResponse, DefaultScopesWhenNoFallback) {
    nlohmann::json j = {
        {"access_token", "at"}
    };

    auto result = oauth::parse_token_response(j.dump());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->scopes.size(), config::kDefaultOAuthScopes.size());
}

// ============================================================
// needs_refresh / has_refresh_token
// ============================================================

TEST(NeedsRefresh, NoRefreshTokenReturnsFalse) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    // no refresh_token set

    EXPECT_FALSE(oauth::needs_refresh(creds));
}

TEST(NeedsRefresh, NoExpiresAtReturnsFalse) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "rt";
    // no expires_at set

    EXPECT_FALSE(oauth::needs_refresh(creds));
}

TEST(NeedsRefresh, NotExpiredReturnsFalse) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "rt";
    creds.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);

    EXPECT_FALSE(oauth::needs_refresh(creds));
}

TEST(NeedsRefresh, ExpiredReturnsTrue) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "rt";
    creds.expires_at = std::chrono::system_clock::now() - std::chrono::seconds(1);

    EXPECT_TRUE(oauth::needs_refresh(creds));
}

TEST(NeedsRefresh, WithinLeewayReturnsTrue) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "rt";
    // Expires 30 seconds from now, but leeway is 60 seconds -> should need refresh
    creds.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(30);

    EXPECT_TRUE(oauth::needs_refresh(creds, std::chrono::system_clock::now(), 60.0));
}

TEST(NeedsRefresh, EmptyRefreshTokenReturnsFalse) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "";  // empty string
    creds.expires_at = std::chrono::system_clock::now() - std::chrono::hours(1);

    EXPECT_FALSE(oauth::needs_refresh(creds));
}

TEST(HasRefreshToken, WithToken) {
    oauth::StoredCredentials creds;
    creds.refresh_token = "rt";
    EXPECT_TRUE(oauth::has_refresh_token(creds));
}

TEST(HasRefreshToken, Empty) {
    oauth::StoredCredentials creds;
    creds.refresh_token = "";
    EXPECT_FALSE(oauth::has_refresh_token(creds));
}

TEST(HasRefreshToken, Nullopt) {
    oauth::StoredCredentials creds;
    EXPECT_FALSE(oauth::has_refresh_token(creds));
}

// ============================================================
// Credentials save/load/delete (temp directory)
// ============================================================

TEST(CredentialsIO, SaveAndLoadRoundTrip) {
    TempDir tmp;

    oauth::StoredCredentials creds;
    creds.access_token = "test_access_token";
    creds.refresh_token = "test_refresh_token";
    creds.expires_at = std::chrono::system_clock::now() + std::chrono::hours(1);
    creds.scopes = {"user:profile", "user:inference"};

    ASSERT_TRUE(oauth::save_credentials(creds, tmp.path));

    auto loaded = oauth::load_credentials(tmp.path, {"default_scope"});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->access_token, creds.access_token);
    ASSERT_TRUE(loaded->refresh_token.has_value());
    EXPECT_EQ(loaded->refresh_token.value(), creds.refresh_token.value());
    ASSERT_TRUE(loaded->expires_at.has_value());
    EXPECT_EQ(loaded->scopes, creds.scopes);

    // Check that expires_at is approximately the same (within 1 second)
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(
        loaded->expires_at.value() - creds.expires_at.value()
    ).count();
    EXPECT_LE(std::abs(diff), 1);
}

TEST(CredentialsIO, SaveAndLoadWithNulls) {
    TempDir tmp;

    oauth::StoredCredentials creds;
    creds.access_token = "token_only";
    // refresh_token = nullopt, expires_at = nullopt

    ASSERT_TRUE(oauth::save_credentials(creds, tmp.path));

    auto loaded = oauth::load_credentials(tmp.path, {"scope"});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->access_token, "token_only");
    EXPECT_FALSE(loaded->refresh_token.has_value());
    EXPECT_FALSE(loaded->expires_at.has_value());
}

TEST(CredentialsIO, DeleteRemovesFiles) {
    TempDir tmp;

    oauth::StoredCredentials creds;
    creds.access_token = "to_delete";
    ASSERT_TRUE(oauth::save_credentials(creds, tmp.path));
    EXPECT_TRUE(std::filesystem::exists(tmp.path / "credentials.json"));

    oauth::delete_credentials(tmp.path);
    EXPECT_FALSE(std::filesystem::exists(tmp.path / "credentials.json"));
}

TEST(CredentialsIO, LoadFromEmptyDirReturnsNullopt) {
    TempDir tmp;
    auto loaded = oauth::load_credentials(tmp.path, {"scope"});
    EXPECT_FALSE(loaded.has_value());
}

TEST(CredentialsIO, LegacyTokenFallback) {
    TempDir tmp;

    // Write a legacy token file
    std::filesystem::create_directories(tmp.path);
    std::ofstream ofs(tmp.path / "token");
    ofs << "  legacy_token_value  \n";
    ofs.close();

    auto loaded = oauth::load_credentials(tmp.path, {"default_scope"});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->access_token, "legacy_token_value");
    EXPECT_FALSE(loaded->refresh_token.has_value());
    EXPECT_FALSE(loaded->expires_at.has_value());
    EXPECT_EQ(loaded->scopes, std::vector<std::string>{"default_scope"});
}

TEST(CredentialsIO, CredentialsJsonTakesPriorityOverLegacy) {
    TempDir tmp;

    // Write both files
    oauth::StoredCredentials creds;
    creds.access_token = "new_token";
    creds.scopes = {"s1"};
    ASSERT_TRUE(oauth::save_credentials(creds, tmp.path));

    std::ofstream ofs(tmp.path / "token");
    ofs << "old_token";
    ofs.close();

    auto loaded = oauth::load_credentials(tmp.path, {"default"});
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->access_token, "new_token");
}

// ============================================================
// JSON conversion
// ============================================================

TEST(CredentialsJson, RoundTrip) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";
    creds.refresh_token = "rt";
    creds.expires_at = std::chrono::system_clock::now() + std::chrono::hours(2);
    creds.scopes = {"a", "b"};

    nlohmann::json j = creds;
    auto restored = j.get<oauth::StoredCredentials>();

    EXPECT_EQ(restored.access_token, creds.access_token);
    EXPECT_EQ(restored.refresh_token, creds.refresh_token);
    EXPECT_EQ(restored.scopes, creds.scopes);
    ASSERT_TRUE(restored.expires_at.has_value());
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(
        restored.expires_at.value() - creds.expires_at.value()
    ).count();
    EXPECT_LE(std::abs(diff), 1);
}

TEST(CredentialsJson, NullFields) {
    oauth::StoredCredentials creds;
    creds.access_token = "at";

    nlohmann::json j = creds;
    EXPECT_TRUE(j["refreshToken"].is_null());
    EXPECT_TRUE(j["expiresAt"].is_null());

    auto restored = j.get<oauth::StoredCredentials>();
    EXPECT_FALSE(restored.refresh_token.has_value());
    EXPECT_FALSE(restored.expires_at.has_value());
}
