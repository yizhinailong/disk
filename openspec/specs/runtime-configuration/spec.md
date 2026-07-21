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

#### Scenario: Stable Redis writer endpoint fails over
- **WHEN** established Redis connections time out while the configured stable writer endpoint moves from a fenced primary to a promoted replica
- **THEN** security-sensitive Redis operations SHALL fail closed during the interruption and the existing API processes SHALL reconnect through the same configured endpoint without restart after the new writer is available

#### Scenario: Rate-limit Redis state is unavailable
- **WHEN** a rate-limit filter cannot read or update Redis limiter state for a request
- **THEN** the system SHALL log the Redis failure and allow the request to continue without returning a rate-limit or server-error response solely because of the Redis limiter failure

### Requirement: File Storage Configuration
The system SHALL initialize file storage from configured storage paths and upload settings.

#### Scenario: Storage initializes
- **WHEN** the backend starts successfully
- **THEN** the system SHALL initialize the active file storage implementation using configured storage and temporary upload paths

### Requirement: S3 Endpoint Origin Validation
The system SHALL treat a custom S3 endpoint as trusted startup-only configuration and SHALL accept only an absolute HTTP or HTTPS origin whose scheme matches `s3.use_ssl`. The endpoint SHALL NOT be selectable or overridden by an API request.

#### Scenario: Valid custom endpoint is configured
- **WHEN** configuration contains a DNS name, IPv4 address, or bracketed IPv6 address with an optional port in the range `1..65535` and no other URL component
- **THEN** startup SHALL pass the validated origin to the S3 client

#### Scenario: Endpoint contains an unsafe or ambiguous component
- **WHEN** a custom endpoint contains a path, trailing slash, userinfo, query, fragment, backslash, percent encoding, whitespace, control character, malformed host, or invalid port
- **THEN** startup SHALL fail before S3 client initialization without echoing the endpoint value in diagnostics

#### Scenario: Private S3 endpoint is deployed
- **WHEN** operators configure an approved private MinIO or S3-compatible origin
- **THEN** the application SHALL allow the origin and deployment controls SHALL restrict DNS and network egress to approved storage services

### Requirement: Upload Task Creation Configuration
The system SHALL expose `upload_task_creation_enabled` as a strict startup boolean, defaulting to enabled, with `DISK_UPLOAD_TASK_CREATION_ENABLED` as its environment override. The effective value SHALL remain fixed for the process lifetime and SHALL control only whether upload initialization may persist a new task; it SHALL NOT select a staging backend or reinterpret existing task descriptors.

#### Scenario: Upload task creation is disabled at startup
- **WHEN** configuration or its environment override sets upload-task creation to false
- **THEN** the process SHALL retain existing-task and instant-upload handling while rejecting initialization that requires a new task

#### Scenario: Upload task creation setting is invalid
- **WHEN** the JSON setting is not boolean or the environment override is not one of the accepted boolean spellings
- **THEN** startup SHALL fail rather than silently enabling task creation

#### Scenario: Operators change the creation setting
- **WHEN** operators need to open or close the upload-task creation cutoff
- **THEN** they SHALL restart or replace every compatible API instance and verify the effective rollout instead of treating the setting as a live runtime switch

### Requirement: Background Task Registration
The system SHALL register cluster maintenance tasks only in a Worker-capable process whose startup configuration enables job claiming. Process-local maintenance MAY remain attached to the process that owns it.

#### Scenario: Claiming Worker begins serving
- **WHEN** a Worker-capable process starts with job claiming enabled
- **THEN** the persistent job claim loop and cluster maintenance task registration SHALL start

#### Scenario: Observation Worker begins serving
- **WHEN** a Worker process starts with job claiming disabled
- **THEN** it SHALL NOT claim, renew, complete, or seed persistent jobs while retaining dependency and queue observation

#### Scenario: API process begins serving
- **WHEN** an API-only process enters its serving lifecycle
- **THEN** it SHALL NOT start the persistent job claim loop or register cluster maintenance tasks

#### Scenario: API process inherits the shared Worker claiming setting
- **WHEN** an API-only process starts from a deployment profile whose shared configuration enables Worker claiming
- **THEN** its effective Worker claiming state SHALL remain disabled and it SHALL NOT initialize, register, or seed cluster maintenance tasks

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
