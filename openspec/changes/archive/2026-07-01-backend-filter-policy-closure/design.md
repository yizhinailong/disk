## Context

The backend has JWT authentication, administrator authorization, share-token authentication, and several Redis-backed fixed-window rate-limit filters. Previous backend cleanup work established that duplicate filter application must be prevented, but the remaining policy choices need to be closed before implementation:

- JWT ownership must move to a single global path with explicit public exemptions.
- Route-level JWT declarations that would overlap with the global JWT filter must be removed.
- Downstream filters and handlers must still receive authenticated user attributes populated by JWT authentication.
- Upload, private download, folder, admin, public share, and register rate-limit families must execute exactly once per matching request.
- Redis failure and rate-limit configuration behavior must be consistent across limiter families.

## Goals / Non-Goals

**Goals:**

- Enforce JWT once for every protected route using global authentication plus explicit exemptions.
- Preserve public auth, health, and public share routes without requiring a bearer token.
- Keep authenticated user attributes available before user-scoped rate-limit filters, admin authorization, and route handlers execute.
- Ensure each named rate-limit family has exactly one application path.
- Standardize rate-limit Redis failure behavior as fail-open with error logging.
- Normalize rate-limit limit/window values behind a single configuration source with code defaults as fallback.

**Non-Goals:**

- Change the external authentication header contract.
- Change share-token authentication semantics for visitor share APIs.
- Replace the fixed-window Redis algorithm with another rate-limit algorithm.
- Introduce a new storage dependency or change Redis client ownership.
- Redesign business authorization beyond removing duplicate JWT filter ownership.

## Decisions

### Decision 1: JWT uses global-with-exemptions ownership

JWT authentication will be registered through the global filter path and will skip only explicit public routes: registration, login, refresh, health, and public share access/browse/download endpoints. Protected routes will not declare JWT at route level.

Rationale: global enforcement makes the default state safe for protected APIs and avoids route-by-route omissions. Explicit exemptions preserve public endpoints while making bypasses auditable.

Alternatives considered:

- Keep route-owned JWT declarations: rejected because the target state requires central enforcement and duplicate route-level declarations are easy to leave behind.
- Apply JWT globally without exemptions: rejected because public auth, health, and public share routes must remain callable without bearer tokens.

### Decision 2: Downstream filters consume global JWT attributes

The global JWT filter remains responsible for writing authenticated user attributes such as user identity, role, and status into request attributes before later filters or handlers need them. Route-level filters that depend on those attributes, such as admin authorization and user-scoped rate limits, must run after global JWT has populated them.

Rationale: this preserves existing handler/filter contracts while removing duplicate JWT declarations.

Alternatives considered:

- Re-read or re-validate JWT in downstream filters: rejected because it violates exactly-once authentication and duplicates token parsing.
- Pass identity through a new context object: rejected as unnecessary scope for this policy closure.

### Decision 3: Rate limits remain path/family scoped and exactly once

Each named rate-limit family will have one documented application path:

- upload: exactly once for upload requests
- private download: exactly once for authenticated private file download requests
- folder: exactly once for folder API requests
- admin: exactly once for administrator API requests
- public share: exactly once for public share access/browse/download requests
- register: exactly once for registration requests

The implementation may use global path-scoped filters or route-level declarations for a family, but not both for the same family. Public share and register rate limits remain available even though those routes are JWT-exempt.

Rationale: exactly-once execution prevents double counting, duplicate Redis increments, and inconsistent `X-RateLimit-*` headers.

Alternatives considered:

- Keep overlapping global and route-level rate-limit declarations: rejected because it causes double counting and ambiguous ownership.
- Collapse all rate limits into a single generic limiter immediately: rejected because existing families have different identifiers, limits, windows, and path scopes.

### Decision 4: Redis rate-limit failures are fail-open with logging

If a rate-limit filter cannot read or update Redis limiter state, it will log the error and allow the request to continue instead of returning 429 or a server error.

Rationale: Redis-backed rate limiting protects the service but should not become a hard availability dependency for normal traffic. Fail-open preserves availability and matches the existing safer behavior for transient Redis failures.

Alternatives considered:

- Fail-closed with 429: rejected because clients could be incorrectly throttled when Redis is unavailable.
- Return 5xx: rejected because rate-limit state failure should not fail otherwise valid business requests.

### Decision 5: Rate-limit configuration is normalized

Each limiter family will load its effective limit/window from one configuration source and use the filter's code default only when configuration is absent or invalid. Configuration names should clearly identify the family and unit, such as per-minute or per-window limits.

Rationale: mixed hard-coded and partially configured values make behavior difficult to audit. A single source per family gives operators predictable control while keeping safe defaults.

Alternatives considered:

- Leave defaults hard-coded only: rejected because operators cannot adjust limits consistently.
- Require every value to be configured: rejected because missing optional limiter values should not prevent startup when safe defaults exist.

## Risks / Trade-offs

- Global JWT exemption matching could accidentally exempt a protected route → Mitigation: centralize exemption predicates and add route/behavior tests for protected and public route families.
- Removing route-level JWT declarations could break filters that assumed JWT appeared in the route list → Mitigation: verify authenticated user attributes are still set before admin/rate-limit filters and handlers run.
- Rate-limit ownership could remain duplicated in a route family → Mitigation: audit global filter registration and route declarations together, then add tests that observe exactly one Redis increment or one limiter execution per request.
- Fail-open Redis policy allows bursts while Redis is unavailable → Mitigation: log failures clearly and keep Redis health observable so operators can respond.
- Configuration normalization can change effective limits if existing values are interpreted differently → Mitigation: preserve current defaults and document each limiter family's unit/window during implementation.

## Migration Plan

1. Add or update centralized JWT exemption predicates for public auth, health, and public share routes.
2. Register JWT globally and remove route-level duplicate JWT declarations from protected routes.
3. Confirm request attributes populated by JWT remain available to admin authorization, user-scoped rate-limit filters, and route handlers.
4. Audit rate-limit ownership and remove overlapping declarations so each named limiter family runs exactly once.
5. Normalize rate-limit limit/window lookups through the runtime configuration source, preserving current defaults.
6. Make Redis failure handling consistent across rate-limit filters as fail-open with error logging.
7. Add tests or verification coverage for protected/public route authentication behavior and exactly-once rate-limit execution.

Rollback strategy: restore route-level JWT declarations and previous rate-limit registration ownership if global enforcement or exemption matching introduces regressions. Because this change does not alter external API contracts or data shape, rollback is code/config-only.

## Open Questions

None. The implementation direction is global JWT with explicit exemptions, fail-open Redis rate-limit behavior, and exactly-once rate-limit execution per named family.
