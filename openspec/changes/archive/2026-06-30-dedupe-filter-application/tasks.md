## 1. Inventory and Ownership

- [x] 1.1 Inventory all backend global filters in `config.json` and all route filters declared through controller `ADD_METHOD_TO` definitions.
- [x] 1.2 Classify each filter as global cross-cutting, global path-scoped, or route-scoped according to `request-filter-application` requirements.
- [x] 1.3 Identify protected routes that currently rely only on global `JwtAuthFilter`, including `POST /api/auth/logout`, and record the route-level filters they need before global JWT removal.

## 2. Route and Configuration Changes

- [x] 2.1 Add explicit route-level `JwtAuthFilter` declarations to any protected routes missing them before removing global JWT enforcement.
- [x] 2.2 Remove `JwtAuthFilter`, `AdminAuthFilter`, and authenticated rate-limit filters from global filter configuration while keeping `RequestTraceFilter` global.
- [x] 2.3 Preserve unauthenticated path-scoped global rate-limit filters that are not duplicated on routes, including register and public-share rate limiters.
- [x] 2.4 Confirm route-scoped filters such as `UploadRateLimitFilter`, `RateLimitFilter`, and `ShareAuthFilter` are not also globally registered.
- [x] 2.5 Preserve filter ordering so `JwtAuthFilter` runs before route filters or handlers that require authenticated user attributes.

## 3. Documentation and Validation

- [x] 3.1 Update documentation that describes protected APIs as using global `JwtAuthFilter` so it reflects route-owned authentication.
- [x] 3.2 Add or update focused validation for public routes remaining public: registration, login, refresh, health, and public share access.
- [x] 3.3 Add or update focused validation for protected routes rejecting missing/invalid bearer tokens, including logout.
- [x] 3.4 Add or update focused validation for admin routes requiring both authenticated user attributes and admin role/status.
- [x] 3.5 Add a static or test-level check that prevents `JwtAuthFilter`, `AdminAuthFilter`, or any rate-limit filter from being applied through both global and route scopes.

## 4. Final Verification

- [x] 4.1 Run the relevant backend test suite or focused filter/controller tests.
- [x] 4.2 Run OpenSpec validation for `dedupe-filter-application`.
- [x] 4.3 Review the final diff to confirm behavior changes are limited to filter ownership and related documentation/tests.
