## 1. Characterization and Discovery

- [x] 1.1 Identify existing backend tests that cover controller response envelopes, authentication, and rate-limit filters
- [x] 1.2 Add or update characterization tests for representative controller success and error responses before helper migration
- [x] 1.3 Verify current JWT and rate-limit filter execution order for protected upload, download, folder, and share routes
- [x] 1.4 Document public, JWT-protected, share-token-protected, and mixed-auth share routes before changing cross-cutting code

## 2. Controller Helpers

- [x] 2.1 Add a small shared controller helper for reading authenticated `user_id` from request attributes
- [x] 2.2 Migrate one simple controller action to the authenticated-user helper as the initial pattern
- [x] 2.3 Migrate remaining `FolderController` actions to the authenticated-user helper
- [x] 2.4 Migrate repetitive `FileController` and `ShareController` authenticated-user reads in small batches
- [x] 2.5 Add result-to-response helper only where it preserves existing response envelopes and endpoint-specific logs

## 3. Service Composition Boundary

- [x] 3.1 Add a small application service context or registry for backend service instances
- [x] 3.2 Initialize the service context after configuration and storage infrastructure are ready
- [x] 3.3 Move `FolderService` construction into the service context and update `FolderController` to consume it
- [x] 3.4 Move `UploadService`, `FileQueryService`, and `FileMutationService` construction into the service context and update `FileController` to consume them
- [x] 3.5 Confirm `UploadService` maintenance startup behavior is not duplicated after composition changes
- [x] 3.6 Move `ShareService` construction into the service context and update `ShareController` to consume it while preserving DB, Redis, JWT-secret, and storage behavior
- [x] 3.7 Include cleanup-related service construction in the context where it is currently constructed directly and safe to centralize

## 4. Rate-limit Implementation Reuse

- [x] 4.1 Add shared fixed-window Redis rate-limit mechanics for increment-with-expiry checks
- [x] 4.2 Add shared construction for `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After` headers
- [x] 4.3 Migrate `UploadRateLimitFilter` to the shared mechanics while preserving its current limit source, key semantics, route scope, and Redis failure behavior
- [x] 4.4 Migrate `DownloadRateLimitFilter` to the shared mechanics while preserving its current default limit, key semantics, route scope, and Redis failure behavior
- [x] 4.5 Migrate `FolderRateLimitFilter` and any register/share-public fixed-window filters to the shared mechanics without changing policy
- [x] 4.6 Keep route-level and global filter declarations unchanged unless characterization proves a duplicate execution bug that is explicitly fixed in this change

## 5. Verification

- [x] 5.1 Run existing backend tests and new characterization tests
- [x] 5.2 Verify public endpoints remain reachable without JWT
- [x] 5.3 Verify JWT-protected endpoints still reject missing, malformed, invalid, and revoked tokens as before
- [x] 5.4 Verify upload, download, and folder rate limits still apply exactly once for representative protected routes
- [x] 5.5 Verify share access, browse, download, and save routes preserve their public, share-token, and mixed-auth behavior
- [x] 5.6 Review the final diff for unrelated formatting churn or behavior changes outside this cleanup scope
