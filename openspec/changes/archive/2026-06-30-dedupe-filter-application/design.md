## Context

The backend currently mixes two filter application mechanisms:

- `config.json` registers several `drogon::plugin::GlobalFilters` entries, including `RequestTraceFilter`, `JwtAuthFilter`, `AdminAuthFilter`, and path-scoped rate-limit filters.
- Controllers also attach filters through `ADD_METHOD_TO`, including repeated `JwtAuthFilter` declarations on protected routes and repeated `AdminAuthFilter` declarations on admin routes.

This means authenticated requests can execute the same JWT and admin checks twice. `JwtAuthFilter` performs bearer-token parsing, access-token verification, revocation lookup, logging, and request attribute insertion. `AdminAuthFilter` is path-guarded to `/api/admin/`, but admin routes also attach it directly, so admin requests can run the same authorization check twice.

The intended security behavior remains unchanged: protected APIs require JWT bearer authentication, admin APIs require admin role/status, public routes stay public, and route-specific share-token authentication remains separate.

## Goals / Non-Goals

**Goals:**

- Remove duplicate execution of the same authentication or authorization filter for one request.
- Make filter ownership easy to reason about: global filters are for cross-cutting concerns, route filters are for endpoint-specific security semantics.
- Preserve existing public/protected route boundaries.
- Preserve existing rate-limit behavior while documenting whether each limiter is global path-scoped or route-scoped.
- Add validation steps that catch accidental reintroduction of duplicate filter application.

**Non-Goals:**

- Redesign JWT token format, token revocation, or role semantics.
- Replace Drogon's filter mechanism or introduce a new middleware framework.
- Change rate-limit thresholds, Redis key formats, or rate-limit algorithms.
- Change public share-token behavior beyond preserving the current route-level `ShareAuthFilter` contract.
- Reorganize controller business logic.

## Decisions

### Decision: Use route-owned JWT and admin authorization

`JwtAuthFilter` and `AdminAuthFilter` will be removed from global filter configuration and kept as explicit route filters.

Rationale:

- Most protected routes already declare `JwtAuthFilter` explicitly in controller route definitions.
- Admin routes already declare `JwtAuthFilter` plus `AdminAuthFilter` explicitly.
- Route-level security makes each endpoint's authentication boundary visible where the route is declared.
- Removing global JWT avoids maintaining a broad exemption list as the primary protection model.

Alternatives considered:

- **Keep JWT global and remove route declarations.** This would make protected APIs default-secure but would hide authentication behavior from route definitions and make correctness depend on the public-route exemption list.
- **Keep both global and route declarations.** This preserves current behavior but retains duplicate work, duplicate logs, and unclear ownership.

### Decision: Keep request tracing global

`RequestTraceFilter` remains globally applied to all requests.

Rationale:

- Request tracing is a true cross-cutting concern and should not depend on each controller remembering to attach it.
- The post-handling advice reads the request id from request attributes for all responses.

### Decision: Treat user-authenticated rate-limit filters as route-scoped filters

Each rate-limit filter has exactly one application path:

- Public or unauthenticated rate limiters may remain global when they do not depend on `JwtAuthFilter` request attributes, such as register and public-share rate limits.
- Authenticated rate limiters that depend on `user_id` must be attached to routes after `JwtAuthFilter`, such as upload, download, admin, folder, and general API/trash rate limits.
- No single rate-limit filter should be both globally registered and attached directly to routes.

Rationale:

- Drogon `GlobalFilters` are registered as pre-routing advice and therefore run before route-level filters.
- If an authenticated rate limiter remains global after JWT becomes route-owned, it would run before `JwtAuthFilter` inserts `user_id` and would silently no-op.
- Route-scoping authenticated rate limiters preserves current enforcement while keeping the route-owned security model.

Alternatives considered:

- **Keep authenticated rate limiters as global path guards.** This only works when global JWT runs before them; it becomes unsafe after JWT is route-owned.
- **Move all rate limiters to global path guards.** This centralizes policy but requires global JWT or duplicated token parsing for authenticated limiters.
- **Make rate limiters independently verify JWT.** This duplicates authentication work and spreads token verification across unrelated filters.

### Decision: Fill route-level authentication gaps before removing global JWT

Before global `JwtAuthFilter` is removed, any endpoint that is protected today only because of the global filter must receive an explicit route-level filter. The known example from exploration is `POST /api/auth/logout`, which is not exempted from global JWT and should declare `JwtAuthFilter` if global JWT is removed.

Rationale:

- The change must be behavior-preserving for protected APIs.
- Removing global JWT first would otherwise make such routes public.

## Risks / Trade-offs

- **Risk: A protected route that relied only on global JWT becomes public.** → Mitigation: inventory all non-public routes before editing `config.json`; add route-level `JwtAuthFilter` where missing; add focused tests for representative protected endpoints including logout.
- **Risk: Documentation still claims JWT protection is global.** → Mitigation: update backend/API documentation references that describe global `JwtAuthFilter` as the protection mechanism.
- **Risk: Future routes forget to declare route-level JWT.** → Mitigation: add validation/documentation that protected routes must declare their required filters and consider a lightweight static check for route definitions.
- **Risk: Rate-limit ownership becomes too broad on route declarations.** → Mitigation: document the accepted split and explicitly assert that an individual filter must not be registered both globally and on routes.
- **Risk: Filter execution order changes for routes that gain explicit filters.** → Mitigation: preserve route filter ordering as `JwtAuthFilter` before filters depending on `user_id`; move authenticated rate limiters to route scope so they run after JWT has populated request attributes.
