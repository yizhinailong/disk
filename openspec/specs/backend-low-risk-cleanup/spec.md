# Backend Low-risk Cleanup Specification

## Purpose

This capability defines low-risk backend cleanup requirements for service composition, controller helper extraction, rate-limit implementation reuse, and preservation of authentication and route behavior.

## Requirements

### Requirement: Service composition boundary

The backend SHALL centralize construction of application services used by controllers behind an application-level composition boundary while preserving existing service lifetimes and startup order.

#### Scenario: Controllers obtain composed services

- **WHEN** a backend controller needs an upload, file-query, file-mutation, folder, share, or cleanup service
- **THEN** the controller SHALL obtain the existing service instance from the application composition boundary instead of directly constructing that service dependency graph

#### Scenario: Service construction side effects are preserved

- **WHEN** services are moved behind the composition boundary
- **THEN** lifecycle-sensitive side effects such as upload-task cache maintenance SHALL occur no more often than they did before the cleanup

#### Scenario: Infrastructure dependencies remain explicit

- **WHEN** composed services need DB, Redis, storage, configuration, or JWT-secret dependencies
- **THEN** those dependencies SHALL be supplied through the composition boundary without changing the externally visible behavior of existing services

### Requirement: Controller helper extraction

The backend SHALL provide small shared controller helpers for mechanical request-handling patterns without changing response envelopes, validation behavior, or endpoint-specific logging.

#### Scenario: Authenticated user id is read consistently

- **WHEN** a JWT-protected controller action needs the authenticated user id
- **THEN** it SHALL read the id through a shared helper that obtains the value populated by `JwtAuthFilter`

#### Scenario: Response envelope is preserved

- **WHEN** a controller action returns a successful or failed service result through a helper
- **THEN** the HTTP response body SHALL continue to use the existing `Response::Success` or `Response::Error` envelope shape

#### Scenario: Validation remains visible

- **WHEN** a controller action parses path, query, or body input
- **THEN** endpoint-specific validation logic and validation error mapping SHALL remain explicit enough to preserve existing behavior and useful diagnostics

### Requirement: Rate-limit implementation reuse

The backend SHALL extract shared fixed-window Redis rate-limit mechanics and rate-limit response-header construction while preserving each endpoint type's current rate-limit policy.

#### Scenario: Fixed-window Redis checks are shared

- **WHEN** an upload, download, folder, register, or share-public rate-limit filter performs a fixed-window counter check
- **THEN** the filter SHALL use shared mechanics for Redis increment-with-expiry handling where practical

#### Scenario: Rate-limit headers are shared

- **WHEN** a request exceeds a fixed-window rate limit
- **THEN** the response SHALL include the existing `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After` headers through shared header construction

#### Scenario: Existing rate-limit policy is preserved

- **WHEN** shared rate-limit mechanics are introduced
- **THEN** each filter SHALL preserve its existing route scope, key semantics, configured or default limit, and Redis failure behavior unless another approved change explicitly modifies those policies

### Requirement: Authentication and route behavior preservation

The cleanup SHALL preserve existing authentication boundaries, route-level filter attachments, public endpoint access, and share-token behavior unless a separate approved policy change modifies them.

#### Scenario: JWT-protected endpoints remain protected

- **WHEN** a route was JWT-protected before the cleanup
- **THEN** the route SHALL remain JWT-protected after the cleanup

#### Scenario: Public endpoints remain public

- **WHEN** a route was intentionally public before the cleanup
- **THEN** the route SHALL remain reachable without JWT after the cleanup

#### Scenario: Share-token endpoints keep share authentication

- **WHEN** a route requires share-token authentication before the cleanup
- **THEN** the route SHALL continue to require share-token authentication after the cleanup

#### Scenario: Filter execution dependencies are respected

- **WHEN** a rate-limit filter depends on `user_id` populated by JWT authentication
- **THEN** the cleanup SHALL preserve an execution order in which JWT authentication can populate `user_id` before that rate-limit filter evaluates the request
