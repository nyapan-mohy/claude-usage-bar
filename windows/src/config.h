#pragma once

#include <array>
#include <cstdint>

namespace config {

// OAuth endpoints
constexpr const char* kAuthorizeEndpoint = "https://claude.ai/oauth/authorize";
constexpr const char* kTokenEndpoint = "https://platform.claude.com/v1/oauth/token";
constexpr const char* kUsageEndpoint = "https://api.anthropic.com/api/oauth/usage";
constexpr const char* kUserinfoEndpoint = "https://api.anthropic.com/api/oauth/userinfo";
constexpr const char* kRedirectUri = "https://platform.claude.com/oauth/code/callback";
constexpr const char* kClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";

// OAuth scopes
inline constexpr std::array<const char*, 2> kDefaultOAuthScopes = {
    "user:profile",
    "user:inference"
};

// Polling intervals (minutes)
inline constexpr std::array<int, 4> kPollingOptions = {5, 15, 30, 60};
constexpr int kDefaultPollingMinutes = 30;

// Backoff
constexpr double kMaxBackoffInterval = 3600.0;  // 1 hour in seconds

// Reset intervals (seconds)
constexpr int64_t kFiveHourResetInterval = 5 * 60 * 60;
constexpr int64_t kSevenDayResetInterval = 7 * 24 * 60 * 60;

// History retention
constexpr int64_t kHistoryRetentionSeconds = 30 * 86400;  // 30 days
constexpr int64_t kHistoryFlushInterval = 300;             // 5 minutes

// HTTP
constexpr const char* kAnthropicBetaHeader = "oauth-2025-04-20";

// App metadata
constexpr const wchar_t* kAppName = L"Claude Usage Bar";
constexpr const char* kConfigDirName = "claude-usage-bar";

// System tray icon
constexpr int kTrayIconSize = 16;

// Notification thresholds
constexpr int kThresholdMin = 0;
constexpr int kThresholdMax = 100;
constexpr int kThresholdStep = 5;

// Credential refresh leeway (seconds before expiry to trigger refresh)
constexpr double kCredentialRefreshLeeway = 60.0;

// PKCE code verifier length (bytes of randomness)
constexpr int kCodeVerifierByteCount = 32;

}  // namespace config
