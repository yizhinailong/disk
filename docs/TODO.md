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

- [x] Update instant upload quota checks and `storage_used` mutation to match logical per-user accounting.
- [x] Update successful private content downloads to increment file-level `download_count` and refresh `last_accessed_at`.
- [x] Update successful share content downloads to preserve share-level counting and also update file-level metadata.
- [x] Add tests that distinguish content downloads from download-info metadata lookups.

---

# Phase 1 — Safety Net Gaps

Completion status: closed by `test(backend): Add cleanup safety coverage` and archived OpenSpec change `2026-07-01-backend-cleanup-safety-tests`. Targeted CMake/Drogon runtime execution was not performed in that worktree; Python syntax checks and OpenSpec validation were recorded in the archived tasks.

## 1.1 Add deterministic cleanup test seams

Current integration coverage is strong, and scheduled cleanup paths now have stable deterministic triggers for safety tests and manual/admin maintenance use.

- [x] Add a stable manual/test-only seam for expired upload cleanup.
- [x] Add a stable manual/test-only seam for expired trash cleanup.
- [x] Keep production behavior unchanged unless explicitly documented.
- [x] Ensure test seams are not exposed unintentionally in production deployments.

## 1.2 Complete expired upload invariant coverage

- [x] Cover expired upload cleanup end-to-end.
- [x] Assert expired upload cleanup releases `users.storage_reserved`.
- [x] Assert expired upload cleanup marks upload task as expired.
- [x] Assert expired upload cleanup removes temporary upload files.
- [x] Assert no `files` row is created for expired uploads.

## 1.3 Complete expired trash invariant coverage

- [x] Cover scheduled expired-trash cleanup.
- [x] Assert expired trash cleanup decrements `file_contents.ref_count`.
- [x] Assert expired trash cleanup releases `users.storage_used` according to the chosen quota rule.
- [x] Assert `ref_count > 0` does not delete physical blob.
- [x] Assert `ref_count == 0` deletes physical blob after zero-ref verification.

## 1.4 Cover upload completion dedup race

- [x] Add a fixture or service seam for “content appears before finalize”.
- [x] Cover upload completion dedup when content is created by another flow before finalization.
- [x] Assert temp assembled file is cleaned when existing content is reused.
- [x] Assert `file_contents.ref_count` is incremented exactly once.
- [x] Assert quota reservation is committed consistently.

---

# Phase 2 — Auth and Rate-limit Policy Closure

Completion status: closed by `refactor(auth): Centralize JWT filter policy` and archived OpenSpec change `2026-07-01-backend-filter-policy-closure`. The implementation uses global JWT with explicit public exemptions, removes duplicate route-level JWT declarations, preserves fail-open Redis limiter behavior, and normalizes limiter configuration lookup. Full CMake/Drogon runtime verification was skipped in that worktree because local configure was blocked by a missing Drogon package.

## 2.1 Choose JWT enforcement strategy

Decision status: `global-with-exemptions`, recorded in `docs/backend-refactor-decisions.md`.

- [x] Decide whether JWT protection should be global-with-exemptions or route-level-only: global-with-exemptions.
- [x] Document the chosen strategy.
- [x] Remove duplicate JWT execution according to the chosen strategy.
- [x] Preserve public auth, health, and public share exemptions.
- [x] Preserve protected upload, file, folder, share-owner, and admin behavior.
- [x] Add or update tests proving JWT executes exactly once for representative protected routes.

## 2.2 Confirm rate-limit execution count

- [x] Verify upload endpoints are upload-rate-limited exactly once.
- [x] Verify private download endpoints are download-rate-limited exactly once.
- [x] Verify folder endpoints are folder-rate-limited exactly once.
- [x] Verify admin endpoints are admin-rate-limited exactly once.
- [x] Verify public share endpoints use the intended public-share limit.
- [x] Verify register endpoint uses the intended register limit.

## 2.3 Finalize Redis failure policy

Decision status: fail-open for all current rate-limit families for now, recorded in `docs/backend-refactor-decisions.md`.

- [x] Decide whether all rate limits should remain fail-open: yes, all current rate-limit families remain fail-open for now.
- [x] If any endpoint should fail-closed, document the reason and expected response: none in this decision pass.
- [x] Keep failure policy explicit in code and tests.
- [x] Ensure headers remain consistent for rate-limit rejection responses.

## 2.4 Normalize limit configuration

- [x] Ensure upload/download/register/share-public/admin/folder limits are configured consistently.
- [x] Avoid duplicated constants across individual filters.
- [x] Keep endpoint-specific path predicates easy to audit.

---

# Phase 3 — Lifecycle Boundary Completion

## 3.1 Finish upload lifecycle extraction

Completion status: closed by `refactor(upload): extract upload lifecycle orchestration`.

- [x] Identify remaining upload lifecycle logic still embedded in `UploadService`.
- [x] Move init/resume/instant-upload orchestration behind explicit lifecycle methods where practical.
- [x] Move complete/finalize orchestration behind explicit lifecycle methods where practical.
- [x] Keep API response construction in controller/service boundary unchanged.
- [x] Keep storage promotion and database finalization compensation explicit.
- [x] Ensure DB failure after blob promotion still deletes promoted final blob.
- [x] Ensure temp cleanup remains idempotent.
- [x] Preserve upload task cache behavior or document any intentional change.

## 3.2 Fix or formalize expired-task handling during init

Completion status: closed by `fix(backend): release quota for expired upload init cleanup`. Inline expired-task handling during upload init now expires the task through `UploadLifecycleService`, releases `storage_reserved` through `QuotaService`, and keeps temp cleanup idempotent.

- [x] Confirm current behavior with a characterization test or direct fixture.
- [x] Decide whether inline expired-task cleanup should release `storage_reserved`: yes, release reserved quota through the upload lifecycle/quota boundary.
- [x] If yes, route the logic through `UploadLifecycleService` / `QuotaService`.
- [x] Ensure expired task deletion, quota release, and temp cleanup cannot drift silently.
- [x] Document compensation behavior for partial failure.

## 3.3 Finish trash lifecycle extraction

Completion status: closed by `refactor(trash): move soft delete orchestration into TrashService`, `test(trash): cover soft delete lifecycle boundaries`, and `refactor(trash): centralize move-to-trash lifecycle`.

- [x] Identify remaining trash lifecycle logic still embedded in `FileMutationService::Delete`.
- [x] Decide whether move-to-trash orchestration should fully live in `TrashService`: yes, `TrashService::MoveToTrash` owns soft-delete lifecycle orchestration.
- [x] If yes, make `FileMutationService::Delete` delegate trash creation/removal orchestration to `TrashService`.
- [x] Keep share cleanup behavior explicit when files/folders move to trash.
- [x] Keep `file_contents.ref_count` decrement only on permanent deletion / expiration cleanup.
- [x] Keep `users.storage_used` decrease only on permanent deletion / expiration cleanup unless product rules change.
- [x] Preserve batch cleanup limits and logging.

## 3.4 Clarify service ownership boundaries

- [x] Treat `ContentService` as the absorbed content persistence boundary; do not revive a separate `ContentRepository`.
- [x] Treat `UploadTaskRepository` as explicit upload-task persistence primitive / repository boundary while keeping lifecycle decisions in services.
- [x] Avoid duplicate paths that mutate the same table with subtly different semantics.
- [x] Document the intended layering:
  - controller
  - application service
  - lifecycle/domain service
  - repository/query object
  - storage abstraction

---

# Phase 4 — Data Access Boundary Expansion

Initial repository primitives exist, but most query/data-access logic is still embedded in services.

## 4.1 Extract file query objects

Completion status: closed by `refactor(file): extract query params and fix list cache invalidation`.

- [x] Extract query object for file list pagination and sorting.
- [x] Extract query object for search.
- [x] Preserve existing sort determinism.
- [x] Preserve existing index usage.
- [x] Ensure cache key behavior includes all relevant query parameters.
- [x] Specifically confirm whether file-list cache keys include `page_size`: yes, `page_size` is part of the file-list cache key.

## 4.2 Expand upload task repository

Completion status: closed by `feat(upload): expand upload task repository` and `refactor(upload): move expiration persistence into repository`.

- [x] Add named repository methods for upload task lookup by id/user.
- [x] Add named repository methods for pending/resumable upload lookup.
- [x] Add named repository methods for status transitions.
- [x] Add named repository methods for chunk coverage.
- [x] Keep lifecycle decision-making outside the repository.

## 4.3 Expand file/folder repositories

Completion status: closed by `refactor(file): extract file folder repository primitives`.

- [x] Add repository methods for common file ownership checks.
- [x] Add repository methods for folder ownership checks.
- [x] Add repository methods for file/folder move path updates.
- [x] Add repository methods for folder subtree queries where useful.
- [x] Keep complex recursive SQL visible and named.

## 4.4 Expand content repository or consolidate into ContentService

- [x] Remove or resolve overlap between `ContentRepository` and `ContentService`.
- [x] Ensure all `file_contents.ref_count` mutations go through one consistent path.
- [x] Ensure zero-ref verification remains explicit before physical blob deletion.
- [x] Preserve current dedup semantics unless product rules change.

## 4.5 Add trash repository/query methods

Completion status: closed by `refactor(trash): extract trash query methods`.

- [x] Extract trash list/count query methods.
- [x] Extract trash item prefetch methods for restore/delete.
- [x] Extract expired trash batch fetch query.
- [x] Keep permanent deletion lifecycle decisions in `TrashService`.

---

# Phase 5 — Transaction Boundary Expansion

Initial `TransactionRunner` exists. Remaining work is to apply it consistently to high-risk flows.

## 5.1 Strengthen upload finalization transaction boundary

Completion status: closed by `fix(upload): guard finalization compensation`.

- [x] Confirm upload finalization uses `TransactionRunner` consistently.
- [x] Ensure content creation/reuse, file row creation, quota commit, task finalization, and chunk cleanup are transactionally grouped where intended.
- [x] Keep filesystem promotion outside the DB transaction unless compensation is explicit.
- [x] Test DB failure after blob promotion compensation.

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

Completion status: closed by `refactor(trash): migrate delete transaction boundary`.

- [x] Wrap move-to-trash record creation, active row removal, share cleanup, and cache invalidation in a clear transaction boundary.
- [x] Keep permanent deletion and blob deletion compensation explicit.
- [x] Ensure storage accounting and ref-count changes occur only at the intended lifecycle stage.

## 5.5 Review exception-to-error mapping

Completion status: closed by `fix(transaction): normalize failure error mapping`.

- [x] Ensure transaction failures map to existing public error shapes.
- [x] Avoid leaking database implementation details.
- [x] Preserve current response envelope shape.

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
- [x] Should inline expired-task cleanup during upload init release `storage_reserved` through the upload lifecycle/quota boundary? Decision: yes; implemented through `UploadLifecycleService` / `QuotaService`.
