## Why

Backend controllers currently construct service dependency graphs directly and repeat common request handling patterns, which makes later domain-boundary extraction harder to review safely. This change performs the Phase 1 low-risk cleanup from `docs/TODO.md` so service composition, controller helpers, and rate-limit implementation reuse can improve without changing public API behavior.

## What Changes

- Introduce an application-level service composition boundary for backend services such as `UploadService`, `FileQueryService`, `FileMutationService`, `FolderService`, `ShareService`, and cleanup-related services.
- Update controllers to obtain existing service instances from the composition boundary instead of constructing dependency graphs directly.
- Add small controller helpers for common authenticated-user and result-to-response patterns where they reduce duplication without hiding validation or degrading logs.
- Extract shared fixed-window Redis rate-limit mechanics and shared `X-RateLimit-*` / `Retry-After` header construction.
- Preserve existing route declarations, authentication behavior, public endpoint exemptions, response envelopes, service lifetimes, and Redis failure behavior unless a later explicit change decides otherwise.
- Keep JWT policy decisions, global-vs-route filter strategy, quota/content/upload/trash semantics, and storage abstraction evolution out of scope.

## Capabilities

### New Capabilities

- `backend-low-risk-cleanup`: Internal backend structure and cross-cutting cleanup requirements that preserve externally visible API behavior while preparing for later domain extraction.

### Modified Capabilities

- None.

## Impact

- Affected code:
  - Backend controllers under `src/controllers/`, especially file, folder, and share controllers.
  - Backend service construction for upload, file query/mutation, folder, share, and cleanup services.
  - Backend filters under `src/filters/` that implement Redis fixed-window rate limiting.
  - Startup/composition code around `src/main.cpp` and storage/config access where needed.
- APIs:
  - No intended public API path, method, response envelope, status-code mapping, or authentication requirement changes.
- Dependencies:
  - No new third-party dependency is expected.
- Tests and verification:
  - Existing backend tests should continue to pass.
  - Add or reuse characterization coverage for controller responses and filter/rate-limit behavior before changing cross-cutting filter declarations.
