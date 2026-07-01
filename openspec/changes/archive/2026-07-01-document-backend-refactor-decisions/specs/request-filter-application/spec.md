## MODIFIED Requirements

### Requirement: Single Filter Ownership
The backend SHALL define one ownership path for each request filter so that the same filter is not applied to a request through both global filter configuration and route-level declarations. JWT authentication SHALL be owned by global filter configuration with explicit public exemptions unless a later architecture decision replaces that strategy.

#### Scenario: Filter ownership reviewed
- **WHEN** a developer reviews global filter configuration and controller route declarations
- **THEN** each filter SHALL have a single documented application path as either global or route-scoped

#### Scenario: Duplicate filter application prevented
- **WHEN** a protected request reaches a controller route
- **THEN** the system SHALL NOT execute the same authentication, authorization, or rate-limit filter twice for that request because of overlapping global and route configuration

#### Scenario: JWT ownership reviewed
- **WHEN** a developer reviews JWT authentication ownership
- **THEN** protected owner and administrator routes SHALL receive JWT through the global filter path and SHALL NOT also declare route-level JWT for duplicate execution

### Requirement: Route-Owned Security Filters
The backend SHALL express endpoint-specific security requirements that are not globally owned, including administrator authorization and share-token authentication, through route-level filter declarations. JWT SHALL remain globally owned with explicit public exemptions under the accepted global-with-exemptions strategy.

#### Scenario: Protected route receives JWT authentication
- **WHEN** a route requires an authenticated user and is not covered by a public exemption
- **THEN** the global JWT authentication filter SHALL authenticate the request before the protected handler depends on authenticated user attributes

#### Scenario: Admin route declares admin authorization
- **WHEN** a route requires administrator access
- **THEN** the route declaration or filter chain SHALL include administrator authorization after JWT authentication has made user role and status attributes available

#### Scenario: Public route remains public
- **WHEN** a route is intentionally public, such as registration, login, refresh, health, or share access
- **THEN** the route declaration and global filter configuration SHALL allow the route to execute without a bearer access token while still applying any route-specific protection such as public rate limiting

### Requirement: Rate Limit Filter Scope
The backend SHALL preserve rate-limit behavior while ensuring each rate-limit filter is applied through exactly one scope: path-scoped global configuration or route-level declaration. Rate-limit Redis increment/check failures SHALL remain fail-open unless a later endpoint-specific policy explicitly changes that behavior.

#### Scenario: Route-scoped rate limiter configured
- **WHEN** a rate limiter is attached directly to a route
- **THEN** that same rate limiter SHALL NOT also be registered as a global filter

#### Scenario: Path-scoped global rate limiter configured
- **WHEN** a rate limiter is registered globally for a specific route family
- **THEN** its implementation SHALL restrict enforcement to that route family and SHALL NOT require per-route attachment

#### Scenario: Redis unavailable during rate-limit check
- **WHEN** a rate-limit filter cannot complete its Redis increment or limit check because Redis is unavailable or returns an error
- **THEN** the filter SHALL allow the request to continue and SHALL make the fail-open behavior explicit through code structure, tests, or logging
