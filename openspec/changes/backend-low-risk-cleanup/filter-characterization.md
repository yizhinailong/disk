# Backend Low-risk Cleanup Route and Filter Characterization

This note captures the route/filter behavior that the cleanup must preserve.

## Existing coverage identified

- Response envelope behavior is covered by `test/utils/Response_test.cpp`.
- JWT token verification and auth error contracts are covered by `test/filters/JwtAuthFilter_test.cpp`.
- Share-token behavior is covered by `test/filters/ShareAuthFilter_test.cpp`.
- Fixed-window path-scope/default-limit behavior is covered by:
  - `test/filters/RegisterRateLimit_test.cpp`
  - `test/filters/DownloadRateLimit_test.cpp`
  - `test/filters/SharePublicRateLimit_test.cpp`
  - `test/filters/AdminRateLimit_test.cpp`
  - `test/filters/FolderRateLimit_test.cpp`
- Integration coverage exists for representative auth, upload, download, folder, share, and upload-rate-limit flows under `test/integration/`.

## Authentication shapes to preserve

- Public routes:
  - `GET /api/health`
  - `POST /api/auth/register`
  - `POST /api/auth/login`
  - `POST /api/auth/refresh`
  - `POST /api/share/access/{share_id}`
- JWT-protected owner/user routes:
  - `/api/file/**`
  - `/api/folder/**`
  - `/api/share` owner endpoints
  - `/api/user/**`
  - `/api/system/info`
  - `/api/logs`
  - `/api/trash/**`
- Admin routes:
  - `/api/admin/**` use `JwtAuthFilter`, then `AdminAuthFilter`, then `AdminRateLimitFilter` at route level.
- Share-token routes:
  - `/api/share/browse/{share_id}`
  - `/api/share/download/{share_id}/{file_id}/info`
  - `/api/share/download/{share_id}/{file_id}`
- Mixed-auth route:
  - `POST /api/share/save/{share_id}` uses both `JwtAuthFilter` and `ShareAuthFilter`.

## Filter-order assumptions

Rate-limit filters that limit by user id depend on `JwtAuthFilter` populating the `user_id` request attribute first. This cleanup does not change route declarations or global filter policy; it only reuses fixed-window Redis mechanics and response-header construction.

## Rate-limit behavior to preserve

- Redis failures remain fail-open in the migrated filters.
- Existing key scopes and route path predicates remain owned by each filter.
- Existing default/configured limits remain owned by each filter.
- Exceeded requests continue to return `TooManyRequests` with their existing header behavior:
  - Upload, download, folder, register, admin, and share-public fixed-window filters include `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After`.
  - The generic API `RateLimitFilter` preserves its previous `X-RateLimit-Limit`, `X-RateLimit-Remaining`, and `X-RateLimit-Reset` headers without adding `Retry-After`.
