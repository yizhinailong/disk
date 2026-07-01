## 1. Safety and Characterization

- [x] 1.1 Add or identify tests covering normal upload init → chunk → complete, including upload task completion, reserved-to-used accounting, file/content rows, and temp cleanup.
- [x] 1.2 Add or identify tests covering upload cancel and expired upload cleanup, including reserved quota release and temp cleanup.
- [x] 1.3 Add or identify tests covering instant upload and finalize-time deduplication, including content ref-count behavior.
- [x] 1.4 Add or identify tests covering copy quota checks and content ref-count increments for files and folder trees.
- [x] 1.5 Add or identify tests covering trash permanent deletion/expiry, used-storage release, content ref-count decrement, and zero-ref blob deletion safety.
- [x] 1.6 Record current behavior for instant upload storage accounting and trash quota retention so reviewers can distinguish preservation from behavior change.

## 2. ContentService Boundary

- [x] 2.1 Create a `ContentService` boundary for `file_contents` lookup, creation, ref-count increment/decrement, batch ref-count updates, and zero-ref verification.
- [x] 2.2 Migrate instant-upload existing-content lookup/ref-count increment to `ContentService` while preserving transaction behavior and responses.
- [x] 2.3 Migrate upload completion dedup lookup, content creation, and existing-content ref-count increment to `ContentService`.
- [x] 2.4 Migrate file/folder copy content ref-count increments to `ContentService`, preserving batch behavior and skip semantics for missing content.
- [x] 2.5 Migrate expired/manual trash content ref-count decrement and zero-ref candidate discovery to `ContentService`.
- [x] 2.6 Keep physical blob deletion call sites explicit and guarded by a fresh zero-ref verification before deletion.

## 3. QuotaService / StorageAccountingService Boundary

- [x] 3.1 Create a quota/accounting service for capacity checks, upload reservation, reserved release, reserved-to-used commit, used-space adjustment, and reconciliation queries.
- [x] 3.2 Migrate upload initialization reservation and create-task failure release paths to the quota service.
- [x] 3.3 Migrate upload cancellation and expired upload cleanup reserved release paths to the quota service.
- [x] 3.4 Migrate upload completion reserved-to-used commit to the quota service while preserving atomicity with file/content/upload-task DB mutations.
- [x] 3.5 Migrate copy quota checks to the quota service while preserving current all-or-skip behavior.
- [x] 3.6 Migrate trash permanent-delete and expiry used-space release to the quota service.
- [x] 3.7 Add a named reconciliation query or diagnostic helper for comparing persisted user accounting to active/trash/content state without changing runtime behavior by default.

## 4. UploadLifecycleService Extraction

- [x] 4.1 Define named upload states and allowed transitions equivalent to the current upload task status behavior.
- [x] 4.2 Extract upload init/resume/instant-upload decision flow behind an upload lifecycle boundary or compatibility facade.
- [x] 4.3 Extract chunk acceptance rules while preserving chunk tracking, duplicate handling, cache behavior, and storage writes.
- [x] 4.4 Extract complete/finalize flow while preserving chunk coverage validation, hash validation, deduplication, storage promotion, DB transaction atomicity, compensation, temp cleanup, cache invalidation, and response shape.
- [x] 4.5 Extract cancel flow while preserving idempotency, reserved release, terminal status, chunk-row cleanup, and temp cleanup.
- [x] 4.6 Move expired upload cleanup orchestration behind the upload lifecycle boundary so scheduled tasks do not implement quota or temp-cleanup rules directly.

## 5. TrashService Lifecycle Consolidation

- [x] 5.1 Move or wrap soft-delete trash record creation under `TrashService` while preserving file/folder namespace removal, folder snapshots, share-link cleanup, and cache invalidation.
- [x] 5.2 Ensure restore behavior continues to resolve unavailable original parents and naming conflicts deterministically.
- [x] 5.3 Consolidate manual permanent delete, empty trash, and expired trash cleanup on shared permanent-delete primitives.
- [x] 5.4 Coordinate permanent delete with `ContentService` for ref-count decrement and zero-ref verification.
- [x] 5.5 Coordinate permanent delete with `QuotaService` for storage-used release only when trash state is permanently deleted.
- [x] 5.6 Preserve batch cleanup limits, logs, and per-chunk failure isolation for scheduled trash expiry.

## 6. Validation and Review

- [x] 6.1 Run focused backend tests after each boundary migration step.
- [x] 6.2 Verify public API response envelopes and error codes remain unchanged unless explicitly documented.
- [x] 6.3 Review database and filesystem failure-domain handling for upload finalization and trash cleanup compensation paths.
- [x] 6.4 Verify cache invalidation remains correct for affected upload, copy, delete, restore, and cleanup flows.
- [x] 6.5 Update design/API/test documentation if any behavior-preserving extraction exposes a previously undocumented invariant.
