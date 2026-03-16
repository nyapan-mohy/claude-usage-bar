// Usage polling service implementation.
// Ported from macOS: UsageService.swift

#include "usage_service.h"
#include "config.h"
#include "settings.h"
#include "string_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace usage_service {

// ============================================================
// UsageServiceState convenience methods
// ============================================================

double UsageServiceState::pct_5h() const {
    if (!usage || !usage->five_hour || !usage->five_hour->utilization) {
        return 0.0;
    }
    return *usage->five_hour->utilization / 100.0;
}

double UsageServiceState::pct_7d() const {
    if (!usage || !usage->seven_day || !usage->seven_day->utilization) {
        return 0.0;
    }
    return *usage->seven_day->utilization / 100.0;
}

double UsageServiceState::pct_extra() const {
    if (!usage || !usage->extra_usage || !usage->extra_usage->utilization) {
        return 0.0;
    }
    return *usage->extra_usage->utilization / 100.0;
}

std::optional<std::chrono::system_clock::time_point> UsageServiceState::reset_5h() const {
    if (!usage || !usage->five_hour) return std::nullopt;
    return usage_model::resets_at_date(*usage->five_hour);
}

std::optional<std::chrono::system_clock::time_point> UsageServiceState::reset_7d() const {
    if (!usage || !usage->seven_day) return std::nullopt;
    return usage_model::resets_at_date(*usage->seven_day);
}

// ============================================================
// Initialization
// ============================================================

UsageServiceState init() {
    UsageServiceState state;

    const auto creds = oauth::load_credentials();
    state.is_authenticated = creds.has_value();

    const auto app_settings = settings::load_settings();
    state.polling_minutes = app_settings.polling_minutes;
    state.current_interval = static_cast<double>(app_settings.polling_minutes) * 60.0;

    return state;
}

// ============================================================
// OAuth flow
// ============================================================

std::string start_oauth_flow(UsageServiceState& state) {
    const auto verifier = oauth::generate_code_verifier();
    const auto challenge = oauth::generate_code_challenge(verifier);
    const auto oauth_state = oauth::generate_code_verifier();  // random state

    state.code_verifier = verifier;
    state.oauth_state = oauth_state;
    state.is_awaiting_code = true;

    return oauth::build_authorize_url(oauth_state, challenge);
}

void submit_oauth_code(
    UsageServiceState& state,
    const std::string& raw_code,
    const http_client::HttpRequestFn& http) {

    const auto parsed = oauth::parse_oauth_code(raw_code);

    // Verify state if present
    if (parsed.state.has_value()) {
        if (!state.oauth_state.has_value() || *parsed.state != *state.oauth_state) {
            state.last_error = "OAuth state mismatch - try again";
            state.is_awaiting_code = false;
            state.code_verifier = std::nullopt;
            state.oauth_state = std::nullopt;
            return;
        }
    }

    if (!state.code_verifier.has_value()) {
        state.last_error = "No pending OAuth flow";
        state.is_awaiting_code = false;
        return;
    }

    // Build token exchange request
    const auto body = oauth::build_token_request_body(
        parsed.code,
        state.oauth_state.value_or(""),
        *state.code_verifier);

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    const auto response = http(config::kTokenEndpoint, "POST", headers, body);
    if (!response) {
        state.last_error = "Token exchange failed: network error";
        return;
    }

    if (response->status_code != 200) {
        state.last_error = "Token exchange failed: HTTP " +
                           std::to_string(response->status_code) + " " +
                           response->body;
        return;
    }

    const auto creds = oauth::parse_token_response(response->body);
    if (!creds) {
        state.last_error = "Could not parse token response";
        return;
    }

    if (!oauth::save_credentials(*creds)) {
        state.last_error = "Failed to save credentials";
        return;
    }

    state.is_authenticated = true;
    state.is_awaiting_code = false;
    state.last_error = std::nullopt;
    state.code_verifier = std::nullopt;
    state.oauth_state = std::nullopt;

    fetch_profile(state, http);
}

void sign_out(UsageServiceState& state) {
    oauth::delete_credentials();
    state.is_authenticated = false;
    state.usage = std::nullopt;
    state.last_updated = std::nullopt;
    state.account_email = std::nullopt;
    state.last_error = std::nullopt;
    state.code_verifier = std::nullopt;
    state.oauth_state = std::nullopt;
    state.is_awaiting_code = false;
}

// ============================================================
// Authorized request helper
// ============================================================

namespace {

std::optional<http_client::HttpResponse> perform_authorized_request(
    const std::string& token,
    const std::string& url,
    const http_client::HttpRequestFn& http) {

    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + token;
    headers["anthropic-beta"] = config::kAnthropicBetaHeader;

    return http(url, "GET", headers, "");
}

bool try_refresh_credentials(const http_client::HttpRequestFn& http) {
    const auto creds = oauth::load_credentials();
    if (!creds || !oauth::has_refresh_token(*creds)) {
        return false;
    }

    const auto body = oauth::build_refresh_request_body(
        *creds->refresh_token, creds->scopes);

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    const auto response = http(config::kTokenEndpoint, "POST", headers, body);
    if (!response || response->status_code != 200) {
        return false;
    }

    const auto updated = oauth::parse_token_response(response->body, &(*creds));
    if (!updated) return false;

    return oauth::save_credentials(*updated);
}

void expire_session(UsageServiceState& state) {
    oauth::delete_credentials();
    state.is_authenticated = false;
    state.usage = std::nullopt;
    state.last_updated = std::nullopt;
    state.account_email = std::nullopt;
    state.last_error = "Session expired - please sign in again";
}

}  // namespace

std::optional<http_client::HttpResponse> send_authorized_request(
    UsageServiceState& state,
    const std::string& url,
    const http_client::HttpRequestFn& http,
    bool expire_session_on_auth_failure) {

    const auto initial_creds = oauth::load_credentials();
    if (!initial_creds) {
        state.last_error = "Not signed in";
        state.is_authenticated = false;
        return std::nullopt;
    }

    // Refresh if token is near expiry
    if (oauth::needs_refresh(*initial_creds)) {
        try_refresh_credentials(http);
    }

    // Use freshest credentials
    const auto active_creds = oauth::load_credentials().value_or(*initial_creds);

    auto result = perform_authorized_request(active_creds.access_token, url, http);
    if (!result) return std::nullopt;

    // If not 401, return as-is
    if (result->status_code != 401) {
        return result;
    }

    // First 401: try refresh
    if (!try_refresh_credentials(http)) {
        if (expire_session_on_auth_failure) {
            expire_session(state);
        }
        return std::nullopt;
    }

    const auto refreshed_creds = oauth::load_credentials();
    if (!refreshed_creds) {
        if (expire_session_on_auth_failure) {
            expire_session(state);
        }
        return std::nullopt;
    }

    result = perform_authorized_request(refreshed_creds->access_token, url, http);
    if (!result) return std::nullopt;

    // Second 401: session is truly expired
    if (result->status_code == 401) {
        if (expire_session_on_auth_failure) {
            expire_session(state);
        }
        return std::nullopt;
    }

    return result;
}

// ============================================================
// API calls
// ============================================================

void fetch_usage(
    UsageServiceState& state,
    const http_client::HttpRequestFn& http) {

    const auto creds = oauth::load_credentials();
    if (!creds) {
        state.last_error = "Not signed in";
        state.is_authenticated = false;
        return;
    }

    const auto result = send_authorized_request(
        state, config::kUsageEndpoint, http);
    if (!result) return;

    // Handle 429 rate limiting
    if (result->status_code == 429) {
        std::optional<double> retry_after;
        const auto it = result->headers.find("retry-after");
        if (it != result->headers.end()) {
            try {
                retry_after = std::stod(it->second);
            } catch (...) {
                // Ignore parse failure, use default
            }
        }
        state.current_interval = usage_model::backoff_interval(
            retry_after, state.current_interval);
        state.last_error = "Rate limited - backing off to " +
                           std::to_string(static_cast<int>(state.current_interval)) + "s";
        return;
    }

    if (result->status_code != 200) {
        state.last_error = "HTTP " + std::to_string(result->status_code);
        return;
    }

    // Parse usage response
    try {
        const auto json = nlohmann::json::parse(result->body);
        auto decoded = json.get<usage_model::UsageResponse>();
        auto reconciled = usage_model::reconcile_response(decoded, state.usage);
        state.usage = reconciled;
        state.last_error = std::nullopt;
        state.last_updated = std::chrono::system_clock::now();

        // Reset backoff to base interval on success
        const double base = base_interval(state);
        if (state.current_interval != base) {
            state.current_interval = base;
        }
    } catch (const std::exception& e) {
        state.last_error = std::string("JSON parse error: ") + e.what();
    }
}

void fetch_profile(
    UsageServiceState& state,
    const http_client::HttpRequestFn& http) {

    // Try local profile first
    const auto local = load_local_profile();
    if (local) {
        state.account_email = *local;
        return;
    }

    // Fall back to userinfo API
    const auto result = send_authorized_request(
        state, config::kUserinfoEndpoint, http, false);
    if (!result || result->status_code != 200) return;

    try {
        const auto json = nlohmann::json::parse(result->body);

        if (json.contains("email") && json["email"].is_string()) {
            const auto email = json["email"].get<std::string>();
            if (!email.empty()) {
                state.account_email = email;
                return;
            }
        }

        if (json.contains("name") && json["name"].is_string()) {
            const auto name = json["name"].get<std::string>();
            if (!name.empty()) {
                state.account_email = name;
                return;
            }
        }
    } catch (...) {
        // Silently ignore profile fetch failures
    }
}

std::optional<std::string> load_local_profile() {
    // Read %USERPROFILE%\.claude.json
    char* userprofile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&userprofile, &len, "USERPROFILE") != 0 || !userprofile) {
        return std::nullopt;
    }
    const auto path = std::filesystem::path(userprofile) / ".claude.json";
    free(userprofile);
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    try {
        const auto json = nlohmann::json::parse(file);
        if (!json.contains("oauthAccount") || !json["oauthAccount"].is_object()) {
            return std::nullopt;
        }

        const auto& account = json["oauthAccount"];

        if (account.contains("emailAddress") && account["emailAddress"].is_string()) {
            const auto email = account["emailAddress"].get<std::string>();
            if (!email.empty()) return email;
        }

        if (account.contains("displayName") && account["displayName"].is_string()) {
            const auto name = account["displayName"].get<std::string>();
            if (!name.empty()) return name;
        }
    } catch (...) {
        // Silently ignore parse failures
    }

    return std::nullopt;
}

// ============================================================
// Polling interval management
// ============================================================

void update_polling_interval(UsageServiceState& state, int minutes) {
    state.polling_minutes = minutes;
    state.current_interval = static_cast<double>(minutes) * 60.0;

    auto app_settings = settings::load_settings();
    app_settings.polling_minutes = minutes;
    settings::save_settings(app_settings);
}

double base_interval(const UsageServiceState& state) {
    return static_cast<double>(state.polling_minutes) * 60.0;
}

}  // namespace usage_service
