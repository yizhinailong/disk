## 1. JWT Enforcement Ownership

- [x] 1.1 Add or update a centralized public-route exemption predicate for registration, login, refresh, health, and public share access/browse/download routes
- [x] 1.2 Register JWT authentication globally so protected routes are authenticated before downstream filters and handlers execute
- [x] 1.3 Remove duplicate route-level JWT declarations from protected user, file, folder, admin, and other non-public routes
- [x] 1.4 Verify JWT-authenticated request attributes remain available to user-scoped rate-limit filters, admin authorization, and protected route handlers

## 2. Rate-Limit Ownership and Policy

- [x] 2.1 Audit upload, private download, folder, admin, public share, and register limiter registration to identify their single owning scope
- [x] 2.2 Remove overlapping global or route-level rate-limit declarations so each named limiter family executes exactly once per matching request
- [x] 2.3 Normalize Redis failure handling across rate-limit filters to fail open with error logging
- [x] 2.4 Normalize rate-limit limit/window lookups through the runtime configuration source while preserving current code defaults as fallback

## 3. Verification

- [x] 3.1 Add or update tests for protected routes without tokens, protected routes with valid tokens, and public JWT-exempt routes
- [x] 3.2 Add or update tests that confirm upload, private download, folder, admin, public share, and register rate limits execute exactly once
- [x] 3.3 Add or update tests for Redis rate-limit failures allowing requests through while logging the failure
- [x] 3.4 Run the backend test/build target used by this repository and record any remaining failures — skipped per user; local CMake configure was blocked by missing Drogon package
