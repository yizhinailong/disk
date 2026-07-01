## Why

Backend lifecycle refactors need stronger protection around cleanup and deduplication paths before later Phase 3/5 work changes internals. The existing safety-net spec already calls out expired upload cleanup, expired trash cleanup, and upload completion deduplication, but the implementation needs deterministic seams and focused coverage so tests do not depend on scheduler timing.

## What Changes

- Add a deterministic test/manual seam for cleanup execution while keeping production scheduled cleanup behavior unchanged.
- Add safety coverage for expired in-progress upload cleanup, including reservation release, terminal upload state, absence of file creation, and temporary artifact cleanup.
- Add safety coverage for expired trash cleanup, including permanent-delete semantics, content reference accounting, storage release, and blob deletion only when references reach zero.
- Add safety coverage for the upload completion deduplication race where matching content appears after upload initialization but before completion.
- Avoid broad lifecycle refactors in this worktree; limit implementation to safety tests and the minimum seam needed to make cleanup paths deterministic.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `backend-safety-net`: Clarify that cleanup safety-net scenarios must be runnable through a deterministic test/manual trigger in addition to unchanged production scheduling, and complete coverage for expired upload cleanup, expired trash cleanup, and upload completion deduplication.

## Impact

- Affected code: backend cleanup orchestration/service entry points, backend integration safety-test helpers, and CTest/uv-backed safety tests as needed.
- APIs: no public user-facing API changes are intended; any new cleanup trigger should be internal/test/manual only.
- Dependencies: no new runtime dependency is expected.
- Systems: upload task cleanup, trash expiry cleanup, quota accounting, content reference accounting, and temporary/blob storage safety invariants.
