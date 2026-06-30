# Backend Refactor TODO

> Goal: Refactor the backend incrementally toward clearer boundaries, stronger consistency, and better testability while preserving current behavior.
>
> This TODO is organized for parallel development. Tasks with no dependency on each other can be worked on concurrently. Prefer small PRs that keep behavior unchanged unless the task explicitly defines a behavior change.

## Guiding Principles

- Preserve existing API behavior unless a task explicitly changes it.
- Add characterization tests before reshaping critical upload/trash/quota/content flows.
- Keep refactors incremental and reviewable.
- Avoid mixing mechanical cleanup with semantic changes in the same PR.
- Treat file-system changes and database changes as separate failure domains; document compensation behavior.

## Parallel Workstream Map

```text
Phase 0: Protection & Discovery
├─ A. Invariant tests and behavior documentation
├─ B. Filter/rate-limit behavior discovery
└─ C. Existing consistency flow mapping

Phase 1: Low-risk structure cleanup
├─ D. ApplicationContext / service composition
├─ E. Controller helper extraction
└─ F. Rate-limit/auth cross-cutting cleanup

Phase 2: Domain boundary extraction
├─ G. ContentService
├─ H. QuotaService / StorageAccountingService
├─ I. UploadLifecycleService
└─ J. TrashService

Phase 3: Data access and transaction boundaries
├─ K. Repository/query extraction
└─ L. TransactionRunner / UnitOfWork

Phase 4: Storage abstraction evolution
└─ M. UploadStagingStorage + BlobStore split
```

## Dependency Overview

```text
A ─┬─▶ D ─▶ G ─┬─▶ I ─┬─▶ K ─▶ L ─▶ M
   │           │      │
   │           └─▶ H ─┘
   │
   └─▶ J ─────────────┘

B ─▶ F

C ─┬─▶ G
   ├─▶ H
   ├─▶ I
   └─▶ J

E can run after A starts, and can proceed independently once response behavior is covered.
```

Legend:

- `A`: invariant tests
- `B`: filter discovery
- `C`: consistency mapping
- `D`: service composition
- `E`: controller helpers
- `F`: cross-cutting filter/rate-limit cleanup
- `G`: content boundary
- `H`: quota boundary
- `I`: upload lifecycle boundary
- `J`: trash lifecycle boundary
- `K`: repositories/query objects
- `L`: transaction runner
- `M`: storage abstraction split

---

# Phase 0 — Protection & Discovery

## A. Add backend invariant tests

**Can run in parallel with:** B, C
**Blocks:** most semantic refactors

### A1. Upload success invariant tests

- [ ] Cover normal upload init → chunk → complete.
- [ ] Assert `upload_tasks.status` becomes completed.
- [ ] Assert `users.storage_reserved` decreases after complete.
- [ ] Assert `users.storage_used` increases according to the current product rule.
- [ ] Assert `files` row is created.
- [ ] Assert `file_contents` row is created or reused.
- [ ] Assert temporary upload files are cleaned after success.

### A2. Upload failure/cancel/expire invariant tests

- [ ] Cover cancel before complete.
- [ ] Cover expired upload cleanup.
- [ ] Assert reserved quota is released on cancel/expire.
- [ ] Assert no `files` row is created on cancel/expire.
- [ ] Assert temp upload files are cleaned.

### A3. Content dedup/ref-count tests

- [ ] Cover instant upload when `file_contents` already exists.
- [ ] Cover upload completion dedup when content appears before finalize.
- [ ] Cover copy operation increments or preserves `file_contents.ref_count` according to current behavior.
- [ ] Cover trash expiration decrements `ref_count`.
- [ ] Cover `ref_count > 0` does not delete physical blob.
- [ ] Cover `ref_count == 0` deletes physical blob during cleanup.

### A4. Quota/accounting tests

- [ ] Cover concurrent-style reservation behavior using `storage_reserved`.
- [ ] Assert upload init rejects when `storage_used + storage_reserved + file_size > storage_quota`.
- [ ] Assert copy checks quota before duplicating logical file records.
- [ ] Assert trash expiration releases used storage according to the current product rule.

### A5. Move/copy/path tests

- [ ] Cover moving file between folders updates path and item counts.
- [ ] Cover moving folder updates subtree folder paths.
- [ ] Cover moving folder updates descendant file paths.
- [ ] Cover moving folder into itself or descendant is rejected.
- [ ] Cover copying folder preserves tree shape and updates content references.

## B. Verify filter and rate-limit behavior

**Can run in parallel with:** A, C
**Blocks:** F

- [ ] Determine whether global filters and route-level filters both execute for protected routes.
- [ ] Document current execution order for `RequestTraceFilter`, `JwtAuthFilter`, admin filters, and rate-limit filters.
- [ ] Confirm public endpoints exempted in `config.json` remain reachable without JWT.
- [ ] Confirm upload endpoints are JWT-protected and upload-rate-limited exactly once.
- [ ] Confirm download endpoints are JWT-protected and download-rate-limited exactly once.
- [ ] Capture findings in this TODO or a design note before changing filter declarations.

## C. Map consistency flows

**Can run in parallel with:** A, B
**Blocks:** G, H, I, J

- [ ] Draw current upload state transitions: init, chunk, complete, cancel, expire.
- [ ] Draw current content lifecycle: create, reuse, ref-count increment, ref-count decrement, blob delete.
- [ ] Draw current quota lifecycle: reserve, release, commit, decrease on trash cleanup.
- [ ] Draw current trash lifecycle: delete to trash, restore if supported, expire, purge.
- [ ] Identify every code path that modifies `users.storage_used`.
- [ ] Identify every code path that modifies `users.storage_reserved`.
- [ ] Identify every code path that modifies `file_contents.ref_count`.
- [ ] Identify every code path that deletes physical files.
- [ ] Record the intended business rule: whether trash items continue to count against quota until permanent deletion.

---

# Phase 1 — Low-risk Structure Cleanup

## D. Introduce application/service composition boundary

**Depends on:** A started; ideally core tests passing
**Can run in parallel with:** E, F after B

- [ ] Introduce an application-level service registry or context for backend services.
- [ ] Centralize construction of `UploadService`, `FileQueryService`, `FileMutationService`, `FolderService`, `ShareService`, and `CleanupService`.
- [ ] Keep `StorageMgr` usage behind the composition boundary where practical.
- [ ] Update controllers to obtain existing service instances instead of directly constructing dependency graphs.
- [ ] Preserve service lifetimes and startup order.
- [ ] Verify existing API behavior with tests.

## E. Extract common controller helpers

**Depends on:** A started
**Can run in parallel with:** D, F

- [ ] Add a common helper for reading authenticated `user_id` from request attributes.
- [ ] Add a common mapping from `Result<T>` to `Response::Success` / `Response::Error` where useful.
- [ ] Add request parsing helpers only if they reduce duplication without hiding validation details.
- [ ] Avoid changing response envelope shape.
- [ ] Migrate one controller method first as a pattern.
- [ ] Migrate remaining repetitive controller methods in small batches.

## F. Unify filter and rate-limit cross-cutting logic

**Depends on:** B

- [ ] Decide whether JWT protection should be declared globally with exemptions or per-route.
- [ ] Remove duplicate JWT execution if confirmed.
- [ ] Extract shared fixed-window Redis rate-limit logic.
- [ ] Extract shared `X-RateLimit-*` and `Retry-After` response header construction.
- [ ] Keep Redis-failure policy explicit: fail-open or fail-closed per endpoint type.
- [ ] Make upload/download/register/share-public limits configurable consistently.
- [ ] Verify public share and auth endpoints remain correctly exempted/protected.

---

# Phase 2 — Domain Boundary Extraction

## G. Extract ContentService

**Depends on:** A3, C

- [ ] Create a `ContentService` boundary for `file_contents` operations.
- [ ] Move content lookup by hash into `ContentService`.
- [ ] Move content creation/reuse into `ContentService`.
- [ ] Move `ref_count` increment/decrement operations into `ContentService`.
- [ ] Move zero-ref content verification into `ContentService`.
- [ ] Keep physical blob deletion safety checks explicit.
- [ ] Update upload, copy, and cleanup flows to use the new boundary incrementally.

## H. Extract QuotaService / StorageAccountingService

**Depends on:** A4, C

- [ ] Create a service for storage quota and accounting operations.
- [ ] Move reservation logic into the service.
- [ ] Move reservation release logic into the service.
- [ ] Move reservation commit logic into the service.
- [ ] Move used-storage increase/decrease logic into the service.
- [ ] Define a reconciliation query for user storage accounting.
- [ ] Add a diagnostic/admin-only reconciliation path if desired later.

## I. Extract UploadLifecycleService

**Depends on:** A1, A2, G, H, C

- [ ] Define explicit upload states and allowed transitions.
- [ ] Centralize init/resume/instant-upload decision flow.
- [ ] Centralize chunk acceptance rules.
- [ ] Centralize complete/finalize flow.
- [ ] Centralize cancel flow.
- [ ] Centralize expire flow or coordinate with cleanup service.
- [ ] Make compensation behavior explicit for DB failure after blob promotion.
- [ ] Keep external API responses unchanged during extraction.

## J. Extract TrashService

**Depends on:** A3, A4, C

- [ ] Centralize move-to-trash behavior.
- [ ] Centralize permanent delete / expired trash cleanup behavior.
- [ ] Define whether trash counts against quota until permanent deletion.
- [ ] Coordinate content ref-count decrement through `ContentService`.
- [ ] Coordinate storage accounting through `QuotaService`.
- [ ] Keep blob deletion behind verified zero-ref checks.
- [ ] Add or preserve batch cleanup limits and logging.

---

# Phase 3 — Data Access and Transaction Boundaries

## K. Extract repositories/query objects

**Depends on:** G, H, I, J mostly stable

- [ ] Extract query object for file list pagination and sorting.
- [ ] Extract query object for search.
- [ ] Extract repository methods for upload tasks.
- [ ] Extract repository methods for files and folders.
- [ ] Extract repository methods for file contents.
- [ ] Extract repository methods for trash.
- [ ] Keep complex SQL visible and named; avoid hiding important query semantics behind vague methods.
- [ ] Ensure query objects preserve existing indexes and sort determinism.

## L. Introduce TransactionRunner / UnitOfWork pattern

**Depends on:** K

- [ ] Add a small transaction helper to standardize begin/commit/rollback behavior.
- [ ] Avoid large framework-style abstraction; keep Drogon transaction semantics understandable.
- [ ] Use it first in one high-value flow, such as upload finalization.
- [ ] Migrate move/copy/delete transactional flows incrementally.
- [ ] Ensure exception-to-error mapping remains consistent.
- [ ] Keep filesystem side effects outside DB transactions unless compensation is clearly documented.

---

# Phase 4 — Storage Abstraction Evolution

## M. Split staging storage from blob storage

**Depends on:** I stable; G stable

- [ ] Define an `UploadStagingStorage` boundary for temp upload sessions.
- [ ] Define a `BlobStore` boundary for final content blobs.
- [ ] Keep local filesystem implementation compatible with current paths.
- [ ] Update upload assembly to depend on staging storage.
- [ ] Update content registration/promotion to depend on blob storage.
- [ ] Update download responder to depend on blob descriptors instead of local filesystem assumptions where practical.
- [ ] Preserve current `build/uploaded/{md5_prefix}/{md5}.bin` layout unless intentionally migrated.
- [ ] Document how an S3/MinIO backend would implement the new interfaces later.

---

# Suggested Parallel Assignments

## Track 1 — Safety Net

- A1 Upload success tests
- A2 Upload failure/cancel/expire tests
- A3 Content ref-count tests
- A4 Quota tests
- A5 Move/copy/path tests

## Track 2 — Architecture Discovery

- B Filter/rate-limit behavior
- C Consistency flow mapping
- Design notes for upload/content/quota/trash rules

## Track 3 — Low-risk Cleanup

- D Application/service composition
- E Controller helpers
- F Rate-limit/auth cleanup after B

## Track 4 — Domain Extraction

- G ContentService
- H QuotaService
- I UploadLifecycleService
- J TrashService

## Track 5 — Infrastructure Evolution

- K Repositories/query objects
- L TransactionRunner
- M Storage abstraction split

---

# Definition of Done

For each task or PR:

- [ ] Existing tests pass.
- [ ] New or updated tests cover changed behavior.
- [ ] Public API response shape is unchanged unless explicitly documented.
- [ ] Database and filesystem side effects are documented for failure cases.
- [ ] Logs remain useful for diagnosing upload, cleanup, and quota issues.
- [ ] Cache invalidation behavior is considered if file/folder/share data changes.
- [ ] No unrelated formatting-only churn is mixed with semantic refactors.

---

# Open Questions

- [ ] Should trash items count against `storage_used` until permanent deletion? Current cleanup behavior suggests yes.
- [ ] Should JWT be enforced globally with explicit public exemptions, or only per route?
- [ ] Should Redis failures remain fail-open for all rate limits, or should some endpoints fail-closed?
- [ ] Should download count and `last_accessed_at` be updated by private downloads, share downloads, or both?
- [ ] Should file-list cache keys include `page_size` if they do not already?
- [ ] Should object storage compatibility be a near-term requirement or only a design constraint?
