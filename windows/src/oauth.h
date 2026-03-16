#pragma once

// OAuth 2.0 PKCE flow, credential storage, and token management.
// Ported from macOS: UsageService.swift (OAuth parts), StoredCredentials.swift

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace oauth {

// ============================================================
// Stored credentials
// ============================================================

struct StoredCredentials {
    std::string access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    std::vector<std::string> scopes;
};

// Check whether the access token should be refreshed
bool needs_refresh(
    const StoredCredentials& creds,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now(),
    double leeway_seconds = 60.0);

bool has_refresh_token(const StoredCredentials& creds);

// JSON conversion
void from_json(const nlohmann::json& j, StoredCredentials& v);
void to_json(nlohmann::json& j, const StoredCredentials& v);

// ============================================================
// Credential file I/O
// ============================================================

// %APPDATA%/claude-usage-bar/
std::filesystem::path get_config_dir();

// Save credentials to credentials.json (creates dir if needed)
bool save_credentials(const StoredCredentials& creds);
bool save_credentials(const StoredCredentials& creds,
                      const std::filesystem::path& dir);

// Load credentials from credentials.json (with legacy token file fallback)
std::optional<StoredCredentials> load_credentials();
std::optional<StoredCredentials> load_credentials(
    const std::filesystem::path& dir,
    const std::vector<std::string>& default_scopes);

// Delete credentials.json and legacy token file
void delete_credentials();
void delete_credentials(const std::filesystem::path& dir);

// ============================================================
// PKCE helpers
// ============================================================

// Generate a 32-byte random code verifier (base64url encoded)
std::string generate_code_verifier();

// SHA256 hash of verifier, base64url encoded
std::string generate_code_challenge(const std::string& verifier);

// Base64url encode raw bytes (no padding)
std::string base64url_encode(const std::vector<uint8_t>& data);
std::string base64url_encode(const uint8_t* data, size_t len);

// ============================================================
// OAuth URL & request builders
// ============================================================

// Build the browser authorize URL with PKCE parameters
std::string build_authorize_url(
    const std::string& state,
    const std::string& code_challenge);

// Parse "code#state" format from OAuth callback
struct OAuthCodeResult {
    std::string code;
    std::optional<std::string> state;
};

OAuthCodeResult parse_oauth_code(const std::string& raw_input);

// Build JSON body for token exchange (authorization_code grant)
std::string build_token_request_body(
    const std::string& code,
    const std::string& state,
    const std::string& code_verifier);

// Build JSON body for token refresh (refresh_token grant)
std::string build_refresh_request_body(
    const std::string& refresh_token,
    const std::vector<std::string>& scopes);

// ============================================================
// Token response parsing
// ============================================================

// Parse token endpoint response JSON into StoredCredentials.
// fallback: previous credentials to inherit refresh_token/scopes from.
std::optional<StoredCredentials> parse_token_response(
    const std::string& json_body,
    const StoredCredentials* fallback = nullptr);

}  // namespace oauth
