## MODIFIED Requirements

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
