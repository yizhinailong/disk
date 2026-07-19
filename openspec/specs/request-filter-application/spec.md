# Request Filter Application Specification

## Purpose

Defines backend request filter ownership, global and route-level filter boundaries, and duplicate filter prevention behavior.

## Requirements

### Requirement: Single Filter Ownership
The backend SHALL define one ownership path for each request filter so that the same filter is not applied to a request through both global filter configuration and route-level declarations. JWT authentication SHALL be owned by global filter configuration with explicit public exemptions unless a later architecture decision replaces that strategy.

#### Scenario: Filter ownership reviewed
- **WHEN** a developer reviews global filter configuration and controller route declarations
- **THEN** each filter SHALL have a single documented application path as either global or route-scoped

#### Scenario: Duplicate filter application prevented
- **WHEN** a protected request reaches a controller route
- **THEN** the system SHALL NOT execute the same authentication, authorization, or rate-limit filter twice for that request because of overlapping global and route configuration

#### Scenario: Global JWT ownership reviewed
- **WHEN** a developer reviews protected route declarations after global JWT authentication is enabled
- **THEN** protected owner and administrator routes SHALL receive JWT through the global filter path and SHALL NOT also declare route-level JWT for duplicate execution

### Requirement: Global Cross-Cutting Filters
The backend SHALL reserve global filters for concerns that must apply consistently across all or broad path-scoped request families without relying on individual route declarations.

#### Scenario: Request tracing applies globally
- **WHEN** any HTTP request is handled
- **THEN** request tracing SHALL be applied globally and SHALL make a request identifier available for response headers and logs

#### Scenario: Path-scoped global filter outside target path
- **WHEN** a path-scoped global filter receives a request outside its target route family
- **THEN** it SHALL no-op without requiring authentication attributes or blocking the request

#### Scenario: JWT applies globally to protected routes
- **WHEN** a request targets a protected route that is not explicitly exempted as public
- **THEN** the global JWT authentication filter SHALL authenticate the bearer access token exactly once before downstream filters or handlers require authenticated user attributes

#### Scenario: Global JWT bypasses public route exemptions
- **WHEN** a request targets public auth, health, or public share access routes
- **THEN** the global JWT authentication filter SHALL allow the request to continue without a bearer access token while preserving any route-specific protection such as rate limiting or share-token validation

### Requirement: Route-Owned Security Filters
The backend SHALL express endpoint-specific security requirements that are not globally owned, including administrator authorization and share-token authentication, through route-level filter declarations while bearer-token authentication is enforced by the global JWT filter.

#### Scenario: Protected route relies on global JWT authentication
- **WHEN** a route requires an authenticated user
- **THEN** the route SHALL rely on the global JWT authentication filter and SHALL NOT declare a duplicate route-level JWT authentication filter

#### Scenario: Admin route declares admin authorization
- **WHEN** a route requires administrator access
- **THEN** the route declaration or filter chain SHALL include administrator authorization after JWT authentication has made user role and status attributes available

#### Scenario: Public route remains public
- **WHEN** a route is intentionally public, such as registration, login, refresh, health, or share access
- **THEN** the route declaration and global filter configuration SHALL allow the route to execute without a bearer access token while still applying any route-specific protection such as public rate limiting

### Requirement: Rate Limit Filter Scope
The backend SHALL preserve rate-limit behavior while ensuring each rate-limit
filter is applied through exactly one scope: path-scoped global configuration or
route-level declaration. Share access, browse, and download rate limiting SHALL be
route owned. Rate-limit Redis increment/check failures SHALL remain fail-open unless
a later endpoint-specific policy explicitly changes that behavior.

#### Scenario: Route-scoped rate limiter configured
- **WHEN** a rate limiter is attached directly to a route
- **THEN** that same rate limiter SHALL NOT also be registered as a global filter

#### Scenario: Path-scoped global rate limiter configured
- **WHEN** a rate limiter is registered globally for a specific route family
- **THEN** its implementation SHALL restrict enforcement to that route family and SHALL NOT require per-route attachment

#### Scenario: Named rate-limit families execute exactly once
- **WHEN** a request matches upload, private download, folder, admin, share access, share browse, share download, or register rate-limit scope
- **THEN** the matching rate-limit family SHALL execute exactly once for that request and SHALL NOT double-count through overlapping route-level and global filter configuration

#### Scenario: Share access limiter ownership
- **WHEN** a request targets `POST /api/share/access/{share_id}`
- **THEN** the route-owned access limiter SHALL execute without requiring owner JWT or Share Token attributes
- **AND** no global public-share limiter SHALL count the request

#### Scenario: Authenticated share limiter ordering
- **WHEN** a request targets share browse, download metadata, download content, or save-to-drive
- **THEN** `ShareAuthFilter` SHALL complete token verification and operation-scope authorization before the route-owned authenticated operation limiter executes
- **AND** the operation limiter SHALL use only the verified `share_token_jti` request attribute

#### Scenario: Save-to-drive filter ordering
- **WHEN** a request targets save-to-drive
- **THEN** global owner JWT authentication SHALL complete before route-level Share Token authentication and share download limiting

#### Scenario: Redis unavailable during rate-limit check
- **WHEN** a rate-limit filter cannot complete its Redis increment or limit check because Redis is unavailable or returns an error
- **THEN** the filter SHALL allow the request to continue and SHALL make the fail-open behavior explicit through code structure, tests, and non-secret logging
