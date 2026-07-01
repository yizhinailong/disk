## MODIFIED Requirements

### Requirement: Redis Connectivity Configuration
The system SHALL use configured Redis connection settings for token, cache, rate-limit, and temporary state behavior, and rate-limit filters SHALL use a consistent fail-open policy when Redis limiter state is unavailable.

#### Scenario: Redis-backed operation executes
- **WHEN** an operation requires Redis-backed state
- **THEN** the system SHALL use the configured Redis client for that state access

#### Scenario: Rate-limit Redis state is unavailable
- **WHEN** a rate-limit filter cannot read or update Redis limiter state for a request
- **THEN** the system SHALL log the Redis failure and allow the request to continue without returning a rate-limit or server-error response solely because of the Redis limiter failure

### Requirement: Public Route Exemptions
The system SHALL configure public APIs such as registration, login, refresh, health, and public share access so they can execute without bearer-token authentication while still applying any route-specific protection.

#### Scenario: Public route requested
- **WHEN** a client calls a configured public route
- **THEN** the system SHALL allow the route to bypass bearer-token authentication while still applying any route-specific protection

#### Scenario: Global authentication is used
- **WHEN** bearer-token authentication is enforced through a global authentication filter
- **THEN** public-route configuration SHALL provide explicit exemptions for public auth, health, and public share access routes and SHALL NOT exempt protected routes

## ADDED Requirements

### Requirement: Rate Limit Configuration
The system SHALL load rate-limit limit and window settings for each configured limiter family from a single runtime configuration source, using safe code defaults only when optional configuration values are absent or invalid.

#### Scenario: Configured limiter value exists
- **WHEN** a rate-limit filter evaluates upload, private download, folder, admin, public share, or register limits and a valid configured value exists for that limiter family
- **THEN** the system SHALL use the configured limit and window values for that limiter family

#### Scenario: Configured limiter value is absent or invalid
- **WHEN** a rate-limit filter evaluates upload, private download, folder, admin, public share, or register limits and no valid configured value exists for that limiter family
- **THEN** the system SHALL use the documented code default for that limiter family
