## Why

JWT and rate-limit filters currently need an explicit policy closure so protected routes are consistently authenticated exactly once and rate-limit filters are not duplicated between global and route-level configuration. This change resolves the remaining backend filter-policy decisions by adopting global JWT with explicit public exemptions, confirming one execution path for each rate-limit family, and making Redis/configuration behavior explicit.

## What Changes

- Adopt a global-with-exemptions JWT enforcement strategy for protected APIs.
- Remove route-level duplicate JWT declarations from routes protected by the global JWT filter while preserving authenticated user attributes for downstream filters and handlers.
- Preserve public auth, health, and public share access exemptions so those endpoints can execute without bearer authentication.
- Confirm exactly-once rate-limit execution for upload, private download, folder, admin, public share, and register flows.
- Define Redis rate-limit failure behavior so the backend responds consistently when Redis-backed limiter state cannot be read or updated.
- Normalize rate-limit configuration so each limiter family reads limit/window values from a single configuration source with safe defaults.

## Capabilities

### New Capabilities

### Modified Capabilities
- `request-filter-application`: Change JWT ownership from route-owned declarations to global-with-exemptions, and require exactly-once execution for each named rate-limit family.
- `identity-and-session`: Update protected API authentication requirements to reflect global JWT enforcement while keeping authenticated user association behavior unchanged.
- `runtime-configuration`: Clarify public route exemptions, Redis failure policy for rate limiting, and normalized rate-limit configuration requirements.

## Impact

- Affected backend filter registration and route declarations for JWT and rate-limit filters.
- Affected protected user/admin/file/folder routes, public auth routes, health routes, and public share routes.
- Affected Redis-backed rate-limit behavior and rate-limit configuration loading/defaults.
- No API header contract changes: protected APIs continue using `Authorization: Bearer <access_token>`, and visitor share APIs continue using `X-Share-Token`.
