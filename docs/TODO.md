# Backend Refactor TODO

> Goal: Finish the remaining backend refactor work toward explicit lifecycle boundaries,
> consistent accounting semantics, safer transaction boundaries, and future storage
> abstraction evolution while preserving current public API behavior unless a task
> explicitly defines a behavior change.
>
> This document tracks remaining work only. Completed historical work has been removed
> from the active checklist to avoid duplicate implementation.

## Current Baseline

The backend already has the following refactor foundations in place:

- Application-level service composition via `ApplicationContext`.
- Controller helper for authenticated `user_id` extraction.
- Shared fixed-window rate-limit helpers.
- Behavior discovery notes for filters, upload/content/quota/trash/download flows.
- `ContentService` for file content lookup, creation, ref-count mutation, and zero-ref verification.
- `QuotaService` for reservation, release, commit, used-storage adjustment, and reconciliation query.
- `UploadLifecycleService` foundation for upload states, init decisions, chunk validation, cancel, and expiration.
- `TrashService` foundation for trash list, restore, permanent delete, delete-all, and expired trash cleanup.
- Initial repository primitives for content and upload tasks.
- Initial `TransactionRunner`.

## Guiding Principles

- Preserve existing API behavior unless a task explicitly changes it.
- Keep refactors incremental and reviewable.
- Avoid mixing mechanical cleanup with semantic changes.
- Treat database changes and filesystem changes as separate failure domains.
- Keep compensation behavior explicit.
- Prefer characterization tests before changing upload, trash, quota, content, or filter behavior.
- Avoid hiding important SQL semantics behind vague repository methods.

---

# Phase 0 — Plan Sync and Baseline Closure

## 0.1 Sync documentation with current implementation

Decision status: closed by `docs/backend-refactor-decisions.md`, with `docs/backend-discovery.md` retained as the source of confirmed current behavior.

- [x] Replace stale historical TODO items with this remaining-work roadmap.
- [x] Link `docs/backend-discovery.md` as the source of current behavior for filters, upload, content, quota, trash, and downloads.
- [x] Move completed historical work to an archive note if long-term traceability is desired; no separate archive note is required for this decision pass.
- [x] Ensure open questions distinguish between:
  - confirmed current behavior
  - future product decisions
  - implementation risks

## 0.2 Reconcile open product semantics

Decision status: closed by `docs/backend-refactor-decisions.md`. Behavior-changing implementation remains open where noted below.

- [x] Decide whether `storage_used` should mean logical per-user bytes or physical unique bytes: logical per-user bytes.
- [x] Decide whether instant upload should increase `storage_used` when copy already does: yes, instant upload should increase logical used storage.
- [x] Decide whether trash items should continue counting against quota until permanent deletion: yes, trash counts against quota until permanent deletion or expiry cleanup.
- [x] Decide whether private downloads should update file-level `download_count` and `last_accessed_at`: yes, successful private content downloads should update file metadata.
- [x] Decide whether share downloads should also update file-level metadata or only share-level metadata: share downloads should update both share-level and file-level metadata.
- [x] Record decisions in a design note before behavior-changing implementation.

Implementation follow-ups:

- [ ] Update instant upload quota checks and `storage_used` mutation to match logical per-user accounting.
- [ ] Update successful private content downloads to increment file-level `download_count` and refresh `last_accessed_at`.
- [ ] Update successful share content downloads to preserve share-level counting and also update file-level metadata.
- [ ] Add tests that distinguish content downloads from download-info metadata lookups.

---

# Phase 1 — Safety Net Gaps

## 1.1 Add deterministic cleanup test seams

Current integration coverage is strong, but scheduled cleanup paths still need stable triggers or fixtures.

- [ ] Add a stable manual/test-only seam for expired upload cleanup.
- [ ] Add a stable manual/test-only seam for expired trash cleanup.
- [ ] Keep production behavior unchanged unless explicitly documented.
- [ ] Ensure test seams are not exposed unintentionally in production deployments.

## 1.2 Complete expired upload invariant coverage

- [ ] Cover expired upload cleanup end-to-end.
- [ ] Assert expired upload cleanup releases `users.storage_reserved`.
- [ ] Assert expired upload cleanup marks upload task as expired.
- [ ] Assert expired upload cleanup removes temporary upload files.
- [ ] Assert no `files` row is created for expired uploads.

## 1.3 Complete expired trash invariant coverage

- [ ] Cover scheduled expired-trash cleanup.
- [ ] Assert expired trash cleanup decrements `file_contents.ref_count`.
- [ ] Assert expired trash cleanup releases `users.storage_used` according to the chosen quota rule.
- [ ] Assert `ref_count > 0` does not delete physical blob.
- [ ] Assert `ref_count == 0` deletes physical blob after zero-ref verification.

## 1.4 Cover upload completion dedup race

- [ ] Add a fixture or service seam for “content appears before finalize”.
- [ ] Cover upload completion dedup when content is created by another flow before finalization.
- [ ] Assert temp assembled file is cleaned when existing content is reused.
- [ ] Assert `file_contents.ref_count` is incremented exactly once.
- [ ] Assert quota reservation is committed consistently.

---

# Phase 2 — Auth and Rate-limit Policy Closure

The shared rate-limit helper work is already in place. Remaining work is mainly policy closure and chain cleanup.

## 2.1 Choose JWT enforcement strategy

Decision status: `global-with-exemptions`, recorded in `docs/backend-refactor-decisions.md`.

Current discovery indicates protected routes may execute both global and route-level JWT filters.

- [x] Decide whether JWT protection should be global-with-exemptions or route-level-only: global-with-exemptions.
- [x] Document the chosen strategy.
- [ ] Remove duplicate JWT execution according to the chosen strategy.
- [ ] Preserve public auth, health, and public share exemptions.
- [ ] Preserve protected upload, file, folder, share-owner, and admin behavior.
- [ ] Add or update tests proving JWT executes exactly once for representative protected routes.

## 2.2 Confirm rate-limit execution count

- [ ] Verify upload endpoints are upload-rate-limited exactly once.
- [ ] Verify private download endpoints are download-rate-limited exactly once.
- [ ] Verify folder endpoints are folder-rate-limited exactly once.
- [ ] Verify admin endpoints are admin-rate-limited exactly once.
- [ ] Verify public share endpoints use the intended public-share limit.
- [ ] Verify register endpoint uses the intended register limit.

## 2.3 Finalize Redis failure policy

Decision status: fail-open for all current rate-limit families for now, recorded in `docs/backend-refactor-decisions.md`.

Current behavior is fail-open for rate-limit Redis failures.

- [x] Decide whether all rate limits should remain fail-open: yes, all current rate-limit families remain fail-open for now.
- [x] If any endpoint should fail-closed, document the reason and expected response: none in this decision pass.
- [ ] Keep failure policy explicit in code and tests.
- [ ] Ensure headers remain consistent for rate-limit rejection responses.

## 2.4 Normalize limit configuration

- [ ] Ensure upload/download/register/share-public/admin/folder limits are configured consistently.
- [ ] Avoid duplicated constants across individual filters.
- [ ] Keep endpoint-specific path predicates easy to audit.

---

# Phase 3 — Lifecycle Boundary Completion

## 3.1 Finish upload lifecycle extraction

`UploadLifecycleService` already owns states, rule helpers, cancel, and expiration. Remaining work is to reduce `UploadService` orchestration responsibility where it still owns too much lifecycle logic.

- [ ] Identify remaining upload lifecycle logic still embedded in `UploadService`.
- [ ] Move init/resume/instant-upload orchestration behind explicit lifecycle methods where practical.
- [ ] Move complete/finalize orchestration behind explicit lifecycle methods where practical.
- [ ] Keep API response construction in controller/service boundary unchanged.
- [ ] Keep storage promotion and database finalization compensation explicit.
- [ ] Ensure DB failure after blob promotion still deletes promoted final blob.
- [ ] Ensure temp cleanup remains idempotent.
- [ ] Preserve upload task cache behavior or document any intentional change.

## 3.2 Fix or formalize expired-task handling during init

Discovery found that inline expired-task cleanup during upload init may delete expired tasks and temp files without visibly releasing reserved quota.

- [ ] Confirm current behavior with a characterization test or direct fixture.
- [ ] Decide whether inline expired-task cleanup should release `storage_reserved`.
- [ ] If yes, route the logic through `UploadLifecycleService` / `QuotaService`.
- [ ] Ensure expired task deletion, quota release, and temp cleanup cannot drift silently.
- [ ] Document compensation behavior for partial failure.

## 3.3 Finish trash lifecycle extraction

`TrashService` already owns major trash operations. Remaining work is to ensure move-to-trash and permanent cleanup responsibilities are not split confusingly across services.

- [ ] Identify remaining trash lifecycle logic still embedded in `FileMutationService::Delete`.
- [ ] Decide whether move-to-trash orchestration should fully live in `TrashService`.
- [ ] If yes, make `FileMutationService::Delete` delegate trash creation/removal orchestration to `TrashService`.
- [ ] Keep share cleanup behavior explicit when files/folders move to trash.
- [ ] Keep `file_contents.ref_count` decrement only on permanent deletion / expiration cleanup.
- [ ] Keep `users.storage_used` decrease only on permanent deletion / expiration cleanup unless product rules change.
- [ ] Preserve batch cleanup limits and logging.

## 3.4 Clarify service ownership boundaries

- [ ] Decide whether `ContentRepository` should remain separate from `ContentService` or be absorbed/renamed.
- [ ] Decide whether `UploadTaskRepository` should remain a low-level primitive or grow into a full repository.
- [ ] Avoid duplicate paths that mutate the same table with subtly different semantics.
- [ ] Document the intended layering:
  - controller
  - application service
  - lifecycle/domain service
  - repository/query object
  - storage abstraction

---

# Phase 4 — Data Access Boundary Expansion

Initial repository primitives exist, but most query/data-access logic is still embedded in services.

## 4.1 Extract file query objects

- [ ] Extract query object for file list pagination and sorting.
- [ ] Extract query object for search.
- [ ] Preserve existing sort determinism.
- [ ] Preserve existing index usage.
- [ ] Ensure cache key behavior includes all relevant query parameters.
- [ ] Specifically confirm whether file-list cache keys include `page_size`.

## 4.2 Expand upload task repository

- [ ] Add named repository methods for upload task lookup by id/user.
- [ ] Add named repository methods for pending/resumable upload lookup.
- [ ] Add named repository methods for status transitions.
- [ ] Add named repository methods for chunk coverage.
- [ ] Keep lifecycle decision-making outside the repository.

## 4.3 Expand file/folder repositories

- [ ] Add repository methods for common file ownership checks.
- [ ] Add repository methods for folder ownership checks.
- [ ] Add repository methods for file/folder move path updates.
- [ ] Add repository methods for folder subtree queries where useful.
- [ ] Keep complex recursive SQL visible and named.

## 4.4 Expand content repository or consolidate into ContentService

- [ ] Remove or resolve overlap between `ContentRepository` and `ContentService`.
- [ ] Ensure all `file_contents.ref_count` mutations go through one consistent path.
- [ ] Ensure zero-ref verification remains explicit before physical blob deletion.
- [ ] Preserve current dedup semantics unless product rules change.

## 4.5 Add trash repository/query methods

- [ ] Extract trash list/count query methods.
- [ ] Extract trash item prefetch methods for restore/delete.
- [ ] Extract expired trash batch fetch query.
- [ ] Keep permanent deletion lifecycle decisions in `TrashService`.

---

# Phase 5 — Transaction Boundary Expansion

Initial `TransactionRunner` exists. Remaining work is to apply it consistently to high-risk flows.

## 5.1 Strengthen upload finalization transaction boundary

- [ ] Confirm upload finalization uses `TransactionRunner` consistently.
- [ ] Ensure content creation/reuse, file row creation, quota commit, task finalization, and chunk cleanup are transactionally grouped where intended.
- [ ] Keep filesystem promotion outside the DB transaction unless compensation is explicit.
- [ ] Test DB failure after blob promotion compensation.

## 5.2 Migrate copy flow transaction boundary

- [ ] Wrap copy quota consumption, file row creation, content ref-count increments, and partial release logic in a clear transaction boundary.
- [ ] Preserve current copy accounting behavior unless product rules change.
- [ ] Ensure partial copy failure cannot leave quota/ref-count drift.

## 5.3 Migrate move flow transaction boundary

- [ ] Wrap file/folder move updates and item-count/path updates in a clear transaction boundary.
- [ ] Preserve subtree path updates.
- [ ] Preserve rejection of moving folder into itself or descendants.
- [ ] Preserve cache invalidation behavior.

## 5.4 Migrate delete/trash transaction boundary

- [ ] Wrap move-to-trash record creation, active row removal, share cleanup, and cache invalidation in a clear transaction boundary.
- [ ] Keep permanent deletion and blob deletion compensation explicit.
- [ ] Ensure storage accounting and ref-count changes occur only at the intended lifecycle stage.

## 5.5 Review exception-to-error mapping

- [ ] Ensure transaction failures map to existing public error shapes.
- [ ] Avoid leaking database implementation details.
- [ ] Preserve current response envelope shape.

---

# Phase 6 — Storage Abstraction Evolution

This phase should wait until upload/content/trash lifecycle boundaries are stable.

## 6.1 Split staging storage from blob storage

- [ ] Define an `UploadStagingStorage` boundary for temporary upload sessions.
- [ ] Define a `BlobStore` boundary for final content blobs.
- [ ] Keep current local filesystem implementation compatible with existing paths.
- [ ] Preserve current `build/uploaded/{md5_prefix}/{md5}.bin` layout unless intentionally migrated.

## 6.2 Move upload assembly to staging storage

- [ ] Make chunk writes depend on staging storage.
- [ ] Make chunk assembly depend on staging storage.
- [ ] Make temp cleanup depend on staging storage.
- [ ] Preserve current temp cleanup idempotency.

## 6.3 Move content promotion to blob storage

- [ ] Make final blob promotion depend on `BlobStore`.
- [ ] Make final blob deletion depend on `BlobStore`.
- [ ] Keep zero-ref verification before deletion.
- [ ] Keep DB failure compensation explicit.

## 6.4 Update download path assumptions

- [ ] Update download responder to depend on blob descriptors where practical.
- [ ] Avoid leaking local filesystem assumptions into controllers.
- [ ] Preserve range download behavior.
- [ ] Preserve current private/share download side effects unless product rules change.

## 6.5 Document object storage compatibility

- [ ] Document how S3/MinIO would implement staging storage.
- [ ] Document how S3/MinIO would implement blob storage.
- [ ] Document consistency tradeoffs for DB commit vs object-store side effects.
- [ ] Decide whether object storage compatibility is a near-term requirement or only a design constraint.

---

# Suggested Dependency Map

```text
Phase 0: Plan Sync and Semantics
├─ 0.1 Sync documentation
└─ 0.2 Reconcile product semantics
       │
       ▼
Phase 1: Safety Net Gaps
├─ 1.1 Cleanup test seams
├─ 1.2 Expired upload coverage
├─ 1.3 Expired trash coverage
└─ 1.4 Upload dedup race coverage
       │
       ├──────────────┐
       ▼              ▼
Phase 2: Auth/Filter   Phase 3: Lifecycle Boundary Completion
├─ JWT strategy        ├─ Upload lifecycle
├─ Rate-limit count    ├─ Expired init handling
└─ Redis policy        └─ Trash lifecycle
                              │
                              ▼
Phase 4: Data Access Boundary Expansion
├─ File query objects
├─ Upload task repository
├─ File/folder repositories
├─ Content repository/service consolidation
└─ Trash repository/query methods
                              │
                              ▼
Phase 5: Transaction Boundary Expansion
├─ Upload finalization
├─ Copy
├─ Move
└─ Delete/trash
                              │
                              ▼
Phase 6: Storage Abstraction Evolution
├─ UploadStagingStorage
└─ BlobStore
```

---

# Definition of Done

For each task or PR:

- [ ] Existing tests pass.
- [ ] New or updated tests cover changed behavior.
- [ ] Public API response shape is unchanged unless explicitly documented.
- [ ] Database and filesystem side effects are documented for failure cases.
- [ ] Logs remain useful for diagnosing upload, cleanup, quota, and trash issues.
- [ ] Cache invalidation behavior is considered if file/folder/share data changes.
- [ ] No unrelated formatting-only churn is mixed with semantic refactors.
- [ ] Behavior changes are separated from mechanical refactors.
- [ ] Cleanup and compensation paths are idempotent where practical.

---

# Remaining Open Questions

These are product or architecture decisions, not merely implementation tasks.

Resolved by `docs/backend-refactor-decisions.md`:

- [x] Should `storage_used` represent logical per-user bytes or physical unique bytes? Decision: logical per-user bytes.
- [x] Should instant upload increase `storage_used` if copy does? Decision: yes.
- [x] Should trash items count against quota until permanent deletion? Decision: yes, until permanent deletion or expiry cleanup.
- [x] Should JWT be enforced globally with explicit public exemptions or only per route? Decision: global with explicit public exemptions.
- [x] Should Redis failures remain fail-open for every rate-limit family? Decision: yes for all current rate-limit families, for now.
- [x] Should private downloads update `files.download_count` and `files.last_accessed_at`? Decision: yes for successful content downloads.
- [x] Should share downloads update file-level metadata, share-level metadata, or both? Decision: both for successful content downloads.

Still open:

- [ ] Should object storage compatibility be a near-term requirement or only a design constraint?
- [ ] Should copy accounting use a reservation-style model instead of incrementing `storage_used` before copy work completes?
- [ ] Should inline expired-task cleanup during upload init release `storage_reserved` through the upload lifecycle/quota boundary?
