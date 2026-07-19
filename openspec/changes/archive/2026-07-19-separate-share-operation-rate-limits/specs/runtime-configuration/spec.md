## MODIFIED Requirements

### Requirement: Rate Limit Configuration
The system SHALL load rate-limit limit and window settings for each configured
limiter family from a single runtime configuration source, using safe code defaults
only when optional configuration values are absent or invalid. Share access,
browse, and download SHALL have independent limit and window settings.

#### Scenario: Configured limiter value exists
- **WHEN** a rate-limit filter evaluates upload, private download, folder, admin, share access, share browse, share download, or register limits and a valid positive configured value exists for that limiter family
- **THEN** the system SHALL use the configured limit and window values for that limiter family

#### Scenario: Configured limiter value is absent or invalid
- **WHEN** a rate-limit filter evaluates upload, private download, folder, admin, share access, share browse, share download, or register limits and no valid positive configured value exists for that limiter family
- **THEN** the system SHALL use the documented code default for that limiter family

#### Scenario: Share operation defaults are used
- **WHEN** share operation settings are absent, zero, negative, or otherwise not usable as positive integers
- **THEN** access SHALL use 30 requests and 60 seconds, browse SHALL use 60 requests and 60 seconds, and download SHALL use 10 requests and 60 seconds

#### Scenario: Share operation settings are configured
- **WHEN** positive values are provided for `share_access_rate_limit_per_minute`, `share_access_rate_limit_window_seconds`, `share_browse_rate_limit_per_minute`, `share_browse_rate_limit_window_seconds`, `share_download_rate_limit_per_minute`, and `share_download_rate_limit_window_seconds`
- **THEN** each share limiter family SHALL use its corresponding configured limit and window without consuming another family's values

#### Scenario: Obsolete public-share settings are present
- **WHEN** `share_public_rate_limit_per_minute` or `share_public_rate_limit_window_seconds` is present at runtime
- **THEN** the system SHALL NOT use either setting as an alias or fallback for access, browse, or download limiting
