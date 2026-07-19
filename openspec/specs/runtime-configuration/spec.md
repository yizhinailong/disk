# Runtime Configuration Specification

## Purpose

Defines backend startup, secure configuration, infrastructure connectivity, storage initialization, background tasks, and public route configuration behavior.

## Requirements

### Requirement: Configuration Loading
The system SHALL load runtime configuration before initializing services that depend on configuration values.

#### Scenario: Application starts
- **WHEN** the backend process starts
- **THEN** the system SHALL load the configured runtime settings before service initialization

### Requirement: Secure Configuration Validation
The system SHALL validate security-sensitive configuration before accepting runtime traffic.

#### Scenario: Required secure configuration is missing
- **WHEN** security-sensitive configuration is missing or invalid during startup
- **THEN** the system SHALL fail startup instead of running with unsafe settings

### Requirement: Database Connectivity Configuration
The system SHALL use configured PostgreSQL connection settings for persistent metadata storage.

#### Scenario: Database-backed operation executes
- **WHEN** an operation requires persistent metadata
- **THEN** the system SHALL use the configured PostgreSQL client for that metadata access

### Requirement: Redis Connectivity Configuration
The system SHALL use configured Redis connection settings for token, cache, rate-limit, and temporary state behavior, and rate-limit filters SHALL use a consistent fail-open policy when Redis limiter state is unavailable.

#### Scenario: Redis-backed operation executes
- **WHEN** an operation requires Redis-backed state
- **THEN** the system SHALL use the configured Redis client for that state access

#### Scenario: Rate-limit Redis state is unavailable
- **WHEN** a rate-limit filter cannot read or update Redis limiter state for a request
- **THEN** the system SHALL log the Redis failure and allow the request to continue without returning a rate-limit or server-error response solely because of the Redis limiter failure

### Requirement: File Storage Configuration
The system SHALL initialize file storage from configured storage paths and upload settings.

#### Scenario: Storage initializes
- **WHEN** the backend starts successfully
- **THEN** the system SHALL initialize the active file storage implementation using configured storage and temporary upload paths

### Requirement: Background Task Registration
The system SHALL register background maintenance tasks during application startup.

#### Scenario: Application begins serving
- **WHEN** the backend application enters its serving lifecycle
- **THEN** scheduled cleanup and token maintenance tasks SHALL be registered

### Requirement: Public Route Exemptions
The system SHALL configure public APIs such as registration, login, refresh, health, and public share access so they can execute without bearer-token authentication while still applying any route-specific protection.

#### Scenario: Public route requested
- **WHEN** a client calls a configured public route
- **THEN** the system SHALL allow the route to bypass bearer-token authentication while still applying any route-specific protection

#### Scenario: Global authentication is used
- **WHEN** bearer-token authentication is enforced through a global authentication filter
- **THEN** public-route configuration SHALL provide explicit exemptions for public auth, health, and public share access routes and SHALL NOT exempt protected routes

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
