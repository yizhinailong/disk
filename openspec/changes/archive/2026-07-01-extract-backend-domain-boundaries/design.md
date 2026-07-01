## Context

The backend currently keeps important consistency rules inside large orchestration methods rather than in explicit domain boundaries. Upload initialization and completion directly perform content lookup/ref-count changes, quota reservation and commit, upload task state changes, temporary storage cleanup, final blob promotion, and compensation. File copy directly increments content refs and checks quota. File delete constructs trash state while cleanup later owns permanent trash expiry, quota release, content ref decrements, and zero-ref blob cleanup.

The extraction should therefore be treated as a behavior-preserving boundary refactor, not as a product-rule redesign. The most important product/accounting rule observed in the current implementation is that `storage_used` is preserved for soft-deleted trash until permanent deletion and instant upload reuses existing content without increasing `storage_used`. If those rules change, that should be proposed separately from this extraction.

## Goals / Non-Goals

**Goals:**

- Make content reference-count operations and zero-reference verification available through a `ContentService` boundary.
- Make storage quota reservation, release, reservation commit, used-space adjustment, quota check, and reconciliation diagnostics available through a `QuotaService` or `StorageAccountingService` boundary.
- Move upload state-transition orchestration into an `UploadLifecycleService` after content and quota primitives are stable.
- Consolidate trash permanent-delete and expiry cleanup semantics under `TrashService` so manual permanent delete, empty trash, and scheduled expiry share content/quota/blob-safety behavior.
- Preserve existing API responses, upload task status meanings, cache invalidation behavior, logs useful for upload/cleanup diagnosis, and filesystem/DB compensation visibility.
- Keep extraction incremental and reviewable, avoiding mechanical churn mixed with business-rule changes.

**Non-Goals:**

- Changing quota product semantics for instant upload, copy, or trash accounting.
- Replacing PostgreSQL/Drogon transaction usage with a broad framework-style unit-of-work abstraction in this change.
- Splitting local storage into object-storage abstractions; that remains a later storage evolution task.
- Reworking controllers or route contracts beyond dependency wiring needed to call the extracted services.
- Removing existing cleanup, cache invalidation, logging, or compensation behavior without equivalent replacement.

## Decisions

1. **Extract domain primitives before lifecycle orchestration.**
   `ContentService` and `QuotaService` should come first because upload and trash lifecycles both depend on them. Existing `UploadService`, `FileMutationService`, `TrashService`, and `CleanupService` can call those primitives before any large orchestration class is introduced. This reduces risk and keeps diffs reviewable.

2. **Preserve current storage accounting semantics.**
   Non-instant upload completion converts reserved bytes to used bytes atomically. Upload cancellation or expiry releases reserved bytes. Soft-deleted trash continues to count against storage until permanent deletion. Instant upload reuses an existing content record without increasing `storage_used`. These rules are treated as current behavior to preserve, even if future product work may revisit whether quota should represent logical bytes rather than physical/deduplicated bytes.

3. **Keep blob deletion safety explicit.**
   `ContentService` may decrement refs and return zero-ref candidates, but physical blob deletion should remain a visible step guarded by a second zero-ref verification immediately before delete. This preserves the current cleanup safety pattern where database state is rechecked after the transaction before calling storage deletion.

4. **Represent upload lifecycle as explicit states and transitions.**
   Upload task statuses should be named in code-level boundaries instead of being spread as integer literals: in-progress, completed, cancelled, and expired. Allowed transitions should remain equivalent to current behavior: init creates in-progress for non-instant uploads; complete finalizes only when chunks and hash are valid; cancel and expire release reservations; already terminal tasks remain idempotent where current behavior permits it.

5. **TrashService should own trash lifecycle semantics, not just controller-facing trash endpoints.**
   Soft-delete creation, restore, manual permanent delete, empty trash, and expired trash cleanup should converge on shared internal primitives. File namespace mutation may still coordinate active file/folder removal and share-link cleanup, but trash state creation and permanent-delete semantics should not be duplicated across services.

6. **Use transactions for database consistency and explicit compensation for filesystem side effects.**
   Database mutations that must be atomic should stay together. Filesystem side effects such as temporary cleanup, final blob promotion, and final blob deletion should remain outside database transactions unless the compensation behavior is documented in the lifecycle method.

## Target Shape

```text
Controllers / ScheduledTasks
          │
          ▼
┌───────────────────────────────────────────────┐
│ Lifecycle / Application Services              │
│ - UploadLifecycleService                      │
│ - TrashService                                │
│ - existing FileMutation/Query/Folder services │
└───────────────┬───────────────────────────────┘
                │ coordinates
                ▼
┌───────────────────────────────────────────────┐
│ Domain Primitives                             │
│ - ContentService                              │
│ - QuotaService / StorageAccountingService     │
└───────────────┬───────────────────────────────┘
                │ uses
                ▼
┌───────────────────────────────────────────────┐
│ PostgreSQL + IFileStorage + cache/log helpers │
└───────────────────────────────────────────────┘
```

## Migration Plan

1. Add characterization tests or focused regression coverage for upload success, cancel/expire, instant upload, finalize dedup, copy ref-counting, trash permanent cleanup, quota reserve/commit/release, and zero-ref blob safety.
2. Introduce `ContentService` with lookup, create, ref increment/decrement, batch ref update, and zero-ref verification primitives. Wire one low-risk call path first, then migrate upload finalize, instant upload, copy, and cleanup ref-count operations.
3. Introduce `QuotaService` with reserve, release, commit reserved-to-used, check quota, adjust used, and reconciliation query primitives. Wire upload init/cancel/expiry/finalize, copy checks, and trash permanent-delete accounting.
4. Extract `UploadLifecycleService` only after content/quota usage is stable. Preserve current upload API DTOs/responses by keeping controller behavior unchanged or using `UploadService` as a compatibility facade during migration.
5. Consolidate trash permanent-delete and expiry cleanup behavior under `TrashService`. Reuse the same content decrement, zero-ref verification, blob deletion, and quota release semantics for manual delete, empty trash, and scheduled expiry.
6. Keep scheduled tasks thin: they should trigger lifecycle cleanup operations rather than reimplement domain rules.
7. After each migration step, run focused tests and inspect logs/compensation behavior for upload and cleanup failure cases.

## Risks / Trade-offs

- **Quota semantics are subtle.** Current instant-upload behavior does not increase `storage_used`, while trash permanent deletion decreases used bytes. This may be surprising if quota is interpreted as logical user-visible bytes. The extraction should document and preserve current behavior rather than silently redefining it.
- **Too much abstraction can hide critical SQL semantics.** Batch content ref updates, quota conditional updates, and cleanup queries should remain named and reviewable, not buried behind vague repository methods.
- **Blob deletion races are dangerous.** The existing re-verification pattern should be preserved so a content ref reclaimed by a concurrent operation is not physically deleted.
- **Upload finalization crosses DB and filesystem failure domains.** Extracting code into services could obscure compensation paths; design and tests should keep promotion/rollback/orphan warnings visible.
- **Trash ownership overlaps with file namespace and sharing cleanup.** Moving soft-delete behavior into `TrashService` must still preserve active namespace removal, share-link cleanup, folder snapshots, and cache invalidation.

## Open Questions

- Should future product behavior redefine quota as logical user-visible bytes, making instant upload and copy increase `storage_used`? Default for this change: no, preserve current behavior.
- Should `ContentService` delete blobs directly in any path, or always return verified deletion candidates to lifecycle code? Default for this change: return candidates and keep deletion explicit.
- Should upload task status constants become an enum-like type in the same change, or remain local named constants until a repository/transaction boundary change? Default for this change: introduce names where they clarify extracted lifecycle code without broad model churn.
