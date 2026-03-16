#pragma once

// Usage polling service — orchestrates OAuth, API calls, and state updates.
// Ported from macOS: UsageService.swift
//
// Design: UsageServiceState struct + free functions (no class).
// The Win32 message loop owns the state and drives transitions.

#include "http_client.h"
#include "oauth.h"
#include "usage_model.h"

#include <chrono>
#include <optional>
#include <string>

namespace usage_service {

// ============================================================
// Service state
// ============================================================

struct UsageServiceState {
    // Current usage data
    std::optional<usage_model::UsageResponse> usage;
    std::optional<std::string> last_error;
    std::optional<std::chrono::system_clock::time_point> last_updated;

    // Authentication
    bool is_authenticated = false;
    bool is_awaiting_code = false;
    std::optional<std::string> account_email;

    // Polling
    int polling_minutes = 30;
    double current_interval = 30 * 60.0;   // seconds

    // PKCE state (only set during an active OAuth flow)
    std::optional<std::string> code_verifier;
    std::optional<std::string> oauth_state;

    // Convenience: derived from usage
    double pct_5h() const;
    double pct_7d() const;
    double pct_extra() const;
    std::optional<std::chrono::system_clock::time_point> reset_5h() const;
    std::optional<std::chrono::system_clock::time_point> reset_7d() const;
};

// ============================================================
// Initialization
// ============================================================

// Create initial state (loads saved credentials to set is_authenticated)
UsageServiceState init();

// ============================================================
// OAuth flow
// ============================================================

// Start browser-based OAuth flow (sets code_verifier, oauth_state)
// Returns the authorize URL to open in the browser.
std::string start_oauth_flow(UsageServiceState& state);

// Submit the code received from OAuth callback.
// Exchanges for token, saves credentials, fetches profile.
void submit_oauth_code(
    UsageServiceState& state,
    const std::string& raw_code,
    const http_client::HttpRequestFn& http);

// Sign out: clear state, delete credentials
void sign_out(UsageServiceState& state);

// ============================================================
// API calls
// ============================================================

// Fetch usage data (authorized GET to usage endpoint)
void fetch_usage(
    UsageServiceState& state,
    const http_client::HttpRequestFn& http);

// Fetch user profile (email/name from userinfo endpoint)
void fetch_profile(
    UsageServiceState& state,
    const http_client::HttpRequestFn& http);

// Try loading email from local .claude.json file
std::optional<std::string> load_local_profile();

// ============================================================
// Authorized request helper
// ============================================================

// Send a request with Bearer token; auto-refreshes if 401.
// Returns nullopt if auth fails completely (session expired).
std::optional<http_client::HttpResponse> send_authorized_request(
    UsageServiceState& state,
    const std::string& url,
    const http_client::HttpRequestFn& http,
    bool expire_session_on_auth_failure = true);

// ============================================================
// Polling interval management
// ============================================================

// Update polling interval (saves to settings)
void update_polling_interval(UsageServiceState& state, int minutes);

// Get the base interval in seconds (from polling_minutes)
double base_interval(const UsageServiceState& state);

}  // namespace usage_service
