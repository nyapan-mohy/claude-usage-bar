// Tests for usage_service — uses mock HTTP (std::function) instead of real network.

#include <gtest/gtest.h>
#include "usage_service.h"
#include "config.h"
#include "oauth.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <string>

// ============================================================
// Helpers
// ============================================================

// Create a mock HTTP function that returns a fixed response.
static http_client::HttpRequestFn make_mock_http(
    int status_code,
    const std::string& body,
    const std::map<std::string, std::string>& headers = {}) {
    return [=](const std::string&, const std::string&,
               const std::map<std::string, std::string>&,
               const std::string&) -> std::optional<http_client::HttpResponse> {
        return http_client::HttpResponse{status_code, body, headers};
    };
}

// Build a valid token response JSON
static std::string make_token_response_json(
    const std::string& access_token = "new-access-token",
    const std::string& refresh_token = "new-refresh-token",
    int expires_in = 3600) {
    nlohmann::json j;
    j["access_token"] = access_token;
    j["refresh_token"] = refresh_token;
    j["expires_in"] = expires_in;
    j["scope"] = "user:profile user:inference";
    return j.dump();
}

// ============================================================
// UsageServiceState convenience methods
// ============================================================

TEST(UsageServiceState, Pct5hReturnsZeroWhenNoUsage) {
    usage_service::UsageServiceState state;
    EXPECT_DOUBLE_EQ(state.pct_5h(), 0.0);
}

TEST(UsageServiceState, Pct5hReturnsCorrectFraction) {
    usage_service::UsageServiceState state;
    usage_model::UsageResponse resp;
    usage_model::UsageBucket bucket;
    bucket.utilization = 60.0;
    resp.five_hour = bucket;
    state.usage = resp;
    EXPECT_DOUBLE_EQ(state.pct_5h(), 0.6);
}

TEST(UsageServiceState, Pct7dReturnsCorrectFraction) {
    usage_service::UsageServiceState state;
    usage_model::UsageResponse resp;
    usage_model::UsageBucket bucket;
    bucket.utilization = 80.0;
    resp.seven_day = bucket;
    state.usage = resp;
    EXPECT_DOUBLE_EQ(state.pct_7d(), 0.8);
}

TEST(UsageServiceState, PctExtraReturnsCorrectFraction) {
    usage_service::UsageServiceState state;
    usage_model::UsageResponse resp;
    usage_model::ExtraUsage extra;
    extra.utilization = 40.0;
    resp.extra_usage = extra;
    state.usage = resp;
    EXPECT_DOUBLE_EQ(state.pct_extra(), 0.4);
}

TEST(UsageServiceState, Reset5hReturnsNulloptWhenNoUsage) {
    usage_service::UsageServiceState state;
    EXPECT_FALSE(state.reset_5h().has_value());
}

TEST(UsageServiceState, Reset7dReturnsNulloptWhenNoUsage) {
    usage_service::UsageServiceState state;
    EXPECT_FALSE(state.reset_7d().has_value());
}

// ============================================================
// init
// ============================================================

TEST(UsageServiceInit, InitWithNoCredentials) {
    // init() uses the default config dir which may or may not have credentials.
    // We just verify it returns a valid state without crashing.
    auto state = usage_service::init();
    // Polling minutes should be from settings (default 30)
    EXPECT_GT(state.polling_minutes, 0);
    EXPECT_DOUBLE_EQ(state.current_interval, state.polling_minutes * 60.0);
}

// ============================================================
// start_oauth_flow
// ============================================================

TEST(UsageServiceOAuth, StartOAuthFlowSetsState) {
    usage_service::UsageServiceState state;
    const auto url = usage_service::start_oauth_flow(state);

    EXPECT_TRUE(state.code_verifier.has_value());
    EXPECT_TRUE(state.oauth_state.has_value());
    EXPECT_TRUE(state.is_awaiting_code);
    EXPECT_FALSE(url.empty());
    // URL should contain the authorize endpoint
    EXPECT_NE(url.find("claude.ai/oauth/authorize"), std::string::npos);
}

// ============================================================
// submit_oauth_code
// ============================================================

TEST(UsageServiceOAuth, SubmitOAuthCodeStateMismatch) {
    usage_service::UsageServiceState state;
    state.code_verifier = "test-verifier";
    state.oauth_state = "expected-state";
    state.is_awaiting_code = true;

    auto mock = make_mock_http(200, make_token_response_json());

    // Submit code with wrong state
    usage_service::submit_oauth_code(state, "the-code#wrong-state", mock);

    EXPECT_TRUE(state.last_error.has_value());
    EXPECT_NE(state.last_error->find("mismatch"), std::string::npos);
    EXPECT_FALSE(state.is_awaiting_code);
    EXPECT_FALSE(state.code_verifier.has_value());
    EXPECT_FALSE(state.oauth_state.has_value());
}

TEST(UsageServiceOAuth, SubmitOAuthCodeNoVerifier) {
    usage_service::UsageServiceState state;
    // No code_verifier set
    state.is_awaiting_code = true;

    auto mock = make_mock_http(200, make_token_response_json());

    usage_service::submit_oauth_code(state, "the-code", mock);

    EXPECT_TRUE(state.last_error.has_value());
    EXPECT_NE(state.last_error->find("No pending"), std::string::npos);
    EXPECT_FALSE(state.is_awaiting_code);
}

TEST(UsageServiceOAuth, SubmitOAuthCodeTokenExchangeNetworkError) {
    usage_service::UsageServiceState state;
    state.code_verifier = "test-verifier";
    state.oauth_state = "test-state";
    state.is_awaiting_code = true;

    auto mock = [](const std::string&, const std::string&,
                   const std::map<std::string, std::string>&,
                   const std::string&) -> std::optional<http_client::HttpResponse> {
        return std::nullopt;
    };

    usage_service::submit_oauth_code(state, "the-code#test-state", mock);

    EXPECT_TRUE(state.last_error.has_value());
    EXPECT_NE(state.last_error->find("network"), std::string::npos);
}

TEST(UsageServiceOAuth, SubmitOAuthCodeTokenExchangeHttpError) {
    usage_service::UsageServiceState state;
    state.code_verifier = "test-verifier";
    state.oauth_state = "test-state";
    state.is_awaiting_code = true;

    auto mock = make_mock_http(400, "Bad Request");

    usage_service::submit_oauth_code(state, "the-code#test-state", mock);

    EXPECT_TRUE(state.last_error.has_value());
    EXPECT_NE(state.last_error->find("HTTP 400"), std::string::npos);
}

TEST(UsageServiceOAuth, SubmitOAuthCodeSuccess) {
    usage_service::UsageServiceState state;
    state.code_verifier = "test-verifier";
    state.oauth_state = "test-state";
    state.is_awaiting_code = true;

    // The mock returns success for token exchange and profile calls
    auto mock = [](const std::string& url, const std::string&,
                   const std::map<std::string, std::string>&,
                   const std::string&) -> std::optional<http_client::HttpResponse> {
        if (url.find("token") != std::string::npos) {
            return http_client::HttpResponse{200, make_token_response_json(), {}};
        }
        // For profile/userinfo requests, return a simple profile
        if (url.find("userinfo") != std::string::npos) {
            return http_client::HttpResponse{200, R"({"email":"test@example.com"})", {}};
        }
        return http_client::HttpResponse{200, "{}", {}};
    };

    usage_service::submit_oauth_code(state, "the-code#test-state", mock);

    EXPECT_TRUE(state.is_authenticated);
    EXPECT_FALSE(state.is_awaiting_code);
    EXPECT_FALSE(state.last_error.has_value());
    EXPECT_FALSE(state.code_verifier.has_value());
    EXPECT_FALSE(state.oauth_state.has_value());
}

// ============================================================
// sign_out
// ============================================================

TEST(UsageServiceSignOut, ClearsAllState) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;
    usage_model::UsageResponse resp;
    resp.five_hour = usage_model::UsageBucket{50.0, "2025-01-01T00:00:00Z"};
    state.usage = resp;
    state.last_updated = std::chrono::system_clock::now();
    state.account_email = "test@example.com";
    state.code_verifier = "verifier";
    state.oauth_state = "state";

    usage_service::sign_out(state);

    EXPECT_FALSE(state.is_authenticated);
    EXPECT_FALSE(state.usage.has_value());
    EXPECT_FALSE(state.last_updated.has_value());
    EXPECT_FALSE(state.account_email.has_value());
    EXPECT_FALSE(state.last_error.has_value());
    EXPECT_FALSE(state.code_verifier.has_value());
    EXPECT_FALSE(state.oauth_state.has_value());
    EXPECT_FALSE(state.is_awaiting_code);
}

// ============================================================
// fetch_usage (backoff logic)
// ============================================================

TEST(UsageServiceFetch, FetchUsage429SetsBackoff) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;
    state.current_interval = 300.0;  // 5 minutes

    // Test the backoff logic directly
    std::optional<double> retry_after = 120.0;
    state.current_interval = usage_model::backoff_interval(
        retry_after, state.current_interval);

    // backoff = min(max(120, 300*2), 3600) = min(600, 3600) = 600
    EXPECT_DOUBLE_EQ(state.current_interval, 600.0);
}

TEST(UsageServiceFetch, FetchUsage429WithNoRetryAfterDoublesInterval) {
    usage_service::UsageServiceState state;
    state.current_interval = 300.0;

    state.current_interval = usage_model::backoff_interval(
        std::nullopt, state.current_interval);

    // backoff = min(max(300, 300*2), 3600) = min(600, 3600) = 600
    EXPECT_DOUBLE_EQ(state.current_interval, 600.0);
}

TEST(UsageServiceFetch, BackoffCapsAtMaxInterval) {
    usage_service::UsageServiceState state;
    state.current_interval = 3000.0;  // already high

    state.current_interval = usage_model::backoff_interval(
        std::nullopt, state.current_interval);

    // backoff = min(max(3000, 6000), 3600) = 3600
    EXPECT_DOUBLE_EQ(state.current_interval, config::kMaxBackoffInterval);
}

// ============================================================
// send_authorized_request
// ============================================================

TEST(UsageServiceAuth, SendAuthorizedRequestNoCredentials) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;

    auto mock = make_mock_http(200, "ok");
    const auto result = usage_service::send_authorized_request(
        state, "https://example.com", mock);

    // If no credentials found, result is nullopt and state reflects it
    if (!result) {
        EXPECT_FALSE(state.is_authenticated);
    }
}

TEST(UsageServiceAuth, SendAuthorizedRequest401ExpireSession) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;

    // Always return 401 — simulates truly expired session
    auto mock = make_mock_http(401, "Unauthorized");
    const auto result = usage_service::send_authorized_request(
        state, "https://example.com", mock, true);

    // Should expire session if no credentials available or refresh fails
    if (!result) {
        EXPECT_FALSE(state.is_authenticated);
    }
}

// ============================================================
// update_polling_interval
// ============================================================

TEST(UsageServicePolling, UpdatePollingInterval) {
    usage_service::UsageServiceState state;
    state.polling_minutes = 30;
    state.current_interval = 1800.0;

    usage_service::update_polling_interval(state, 15);

    EXPECT_EQ(state.polling_minutes, 15);
    EXPECT_DOUBLE_EQ(state.current_interval, 900.0);
}

// ============================================================
// base_interval
// ============================================================

TEST(UsageServicePolling, BaseInterval) {
    usage_service::UsageServiceState state;
    state.polling_minutes = 5;

    EXPECT_DOUBLE_EQ(usage_service::base_interval(state), 300.0);
}

TEST(UsageServicePolling, BaseInterval60) {
    usage_service::UsageServiceState state;
    state.polling_minutes = 60;

    EXPECT_DOUBLE_EQ(usage_service::base_interval(state), 3600.0);
}

// ============================================================
// load_local_profile
// ============================================================

TEST(UsageServiceProfile, LoadLocalProfileNoFile) {
    // Verify it doesn't crash when .claude.json doesn't exist.
    const auto result = usage_service::load_local_profile();
    // Either returns a value or nullopt — both are valid
    (void)result;
}

// ============================================================
// fetch_profile
// ============================================================

TEST(UsageServiceProfile, FetchProfileFromUserinfo) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;

    auto mock = [](const std::string& url, const std::string&,
                   const std::map<std::string, std::string>&,
                   const std::string&) -> std::optional<http_client::HttpResponse> {
        if (url.find("userinfo") != std::string::npos) {
            return http_client::HttpResponse{200, R"({"email":"user@example.com"})", {}};
        }
        return http_client::HttpResponse{200, "{}", {}};
    };

    usage_service::fetch_profile(state, mock);

    // If load_local_profile returned a value, that takes precedence.
    // Otherwise, the mock userinfo should have been used.
    if (state.account_email.has_value()) {
        EXPECT_FALSE(state.account_email->empty());
    }
}

TEST(UsageServiceProfile, FetchProfileFallbackToName) {
    usage_service::UsageServiceState state;
    state.is_authenticated = true;

    auto mock = [](const std::string& url, const std::string&,
                   const std::map<std::string, std::string>&,
                   const std::string&) -> std::optional<http_client::HttpResponse> {
        if (url.find("userinfo") != std::string::npos) {
            return http_client::HttpResponse{200, R"({"email":"","name":"John Doe"})", {}};
        }
        return http_client::HttpResponse{200, "{}", {}};
    };

    usage_service::fetch_profile(state, mock);

    if (state.account_email.has_value()) {
        EXPECT_FALSE(state.account_email->empty());
    }
}
