## Why

Backend upload, content deduplication, storage accounting, and trash cleanup behavior is currently correct but concentrated across large service flows. The same invariants are implemented directly in upload completion, instant upload, copy, soft delete, expired upload cleanup, and expired trash cleanup. This makes future changes risky because `file_contents.ref_count`, `users.storage_used`, `users.storage_reserved`, upload task status, and physical blob deletion safety can drift between code paths.

The backend refactor TODO identifies Phase 2 domain extraction as the next architecture step after safety/discovery work. This change captures the domain-boundary target before implementation so extraction can remain behavior-preserving and reviewable.

## What Changes

- Introduce backend domain boundaries for content reference management, storage quota/accounting, upload lifecycle orchestration, and trash lifecycle ownership.
- Preserve existing public API behavior and response shapes while moving direct SQL/accounting/ref-count responsibilities behind explicit services.
- Treat current storage accounting semantics as the behavior to preserve: non-instant upload completion converts reserved bytes to used bytes; cancellation/expiry releases reserved bytes; soft-deleted trash continues to count until permanent deletion; instant upload reuses existing content without increasing `storage_used` unless a later explicit behavior change says otherwise.
- Keep physical blob deletion guarded by explicit zero-reference verification instead of hiding unsafe deletion behind generic helpers.
- Stage extraction incrementally: first ContentService and QuotaService primitives, then wire existing flows to them, then extract UploadLifecycleService and consolidate trash permanent-delete/expiry behavior.
- Add focused characterization and regression coverage for upload success/failure, content ref-counts, quota accounting, trash permanent deletion, and compensation behavior.

## Capabilities

### Modified Capabilities

- `architecture-decisions`: Record accepted backend domain-boundary decisions and constraints for future refactors.
- `file-transfer`: Clarify upload lifecycle, quota reservation, instant upload, and completion invariants that the extracted services must preserve.
- `trash-lifecycle`: Clarify trash ownership, permanent-delete semantics, quota release timing, and blob cleanup safety.
- `persistence-design`: Clarify persistence-level ownership of storage accounting and content reference-count mutations.
- `validation-and-performance`: Require characterization/regression coverage for domain extraction invariants.

## Impact

- Backend services under `src/services/`, especially upload, file mutation/copy/delete, trash, cleanup, and scheduled cleanup flows.
- Storage boundary usage under `src/storage/` where temporary upload artifacts and final content blobs are cleaned or promoted.
- PostgreSQL tables: `file_contents`, `files`, `folders`, `users`, `upload_tasks`, `upload_task_chunks`, and `trash`.
- Backend tests for upload lifecycle, dedup/ref-count behavior, quota accounting, trash cleanup, and failure compensation.
- No API contract break is intended; this is an internal architecture extraction with explicit behavior preservation.
