# Backend Refactor Decisions

This note records accepted product and policy decisions for the backend refactor roadmap. It is intentionally documentation-only: it does not mean the runtime behavior has already changed.

Use `docs/backend-discovery.md` as the source of confirmed current implementation behavior. When this note differs from discovery, discovery describes the current behavior and this note describes the accepted target for a later implementation change.

## Decision Summary

| Area | Accepted decision | Current implementation status | Follow-up status |
| --- | --- | --- | --- |
| `storage_used` meaning | Logical per-user bytes | Mixed semantics across instant upload and copy | Implement accounting cleanup later |
| Instant upload accounting | Increase `storage_used` like copy | Does not increase `storage_used` today | Behavior-changing implementation required |
| Trash quota | Trash counts against quota until permanent delete / expiry cleanup | Already matches current behavior | Preserve and test in later cleanup work |
| Private download metadata | Successful private content download updates file metadata | Does not update file metadata today | Behavior-changing implementation required |
| Share download metadata | Successful share content download updates share-level and file-level metadata | Updates share-level count only today | Behavior-changing implementation required |
| JWT enforcement | Global JWT with explicit public exemptions | Global and route-level JWT can both run | Filter cleanup implementation required |
| Redis rate-limit failure | Fail-open for now | Already fail-open today | Make explicit in code/tests later |
| Copy accounting | Reserve candidate logical bytes before copy work, then commit successful bytes to used storage and release skipped/failed bytes | Pre-increments `storage_used` before copy work and compensates skipped/failed bytes with later used-storage decrements | Behavior-changing implementation required |

## Decision: `storage_used` means logical per-user bytes

**Current implementation behavior:** `docs/backend-discovery.md` records mixed semantics. Upload completion increases `users.storage_used`, copy pre-check/reservation increases `users.storage_used`, but instant upload reusing existing content does not increase `users.storage_used`.

**Accepted target behavior:** `users.storage_used` means logical bytes charged to a user-owned namespace, not globally unique physical bytes stored in `file_contents`. Deduplication remains an internal storage optimization and does not reduce the user's logical quota usage.

**Rationale:** Logical per-user bytes match user expectations and current copy behavior. Users see files in their namespace and quota should reflect that visible ownership rather than hidden physical deduplication state.

**Rejected alternatives:**

- Physical unique bytes: rejected because quota would depend on global content reuse and would conflict with existing copy accounting.
- Hybrid per-operation accounting: rejected because upload, instant upload, copy, and trash would be difficult to explain and test consistently.

**Implementation impact:** Later accounting work should make quota checks and `storage_used` mutations consistently use logical per-user bytes. Reconciliation tooling should compare stored usage against logical active-plus-trash file sizes rather than physical unique content size.

**Follow-up status:** Decision recorded; runtime cleanup remains open.

## Decision: instant upload increases `storage_used`

**Current implementation behavior:** `docs/backend-discovery.md` records that instant upload increments `file_contents.ref_count` and inserts a `files` row, but does not increase `users.storage_used`.

**Accepted target behavior:** Instant upload creates a new logical file reference and must increase `users.storage_used` by the logical file size, subject to quota checks, consistently with copy semantics.

**Rationale:** Without this rule, users can bypass logical quota when content already exists physically. Aligning instant upload with copy makes quota behavior independent of deduplication hits.

**Rejected alternatives:**

- Preserve current no-increase behavior: rejected because it conflicts with logical per-user bytes and copy behavior.
- Defer quota checks until later file activity: rejected because quota enforcement should occur when the new file reference is created.

**Implementation impact:** Later upload lifecycle work must update the instant-upload path to check available logical quota, increment used storage atomically with file row creation and content ref-count increment, and add characterization/regression tests.

**Follow-up status:** Decision recorded; behavior-changing implementation remains open.

## Decision: copy accounting uses reservation-style commit/release

**Current implementation behavior:** `FileMutationService::Copy` computes `total_copy_size`, calls `QuotaService::ConsumeUsedStorage` before copy batches run, and then compensates skipped or failed work by decrementing `users.storage_used`. The current transaction boundary groups content ref-count increments, copied file/folder row creation, and some partial release logic, but the initial used-storage increment remains a separate pre-copy accounting step.

**Accepted target behavior:** Copy should reserve candidate logical bytes in `users.storage_reserved` before copy work starts, then transfer only successfully copied logical bytes from reserved to used storage in the same database transaction that increments `file_contents.ref_count` and creates the copied `files` / `folders` rows. Bytes for skipped items, rejected folders, missing content, and failed copy units must be released from `storage_reserved`, not subtracted from `storage_used`.

**Compatibility and API impact:** Public API response shape and visible partial-copy behavior stay unchanged. Copy may still return success with fewer copied items when conflicts, invalid IDs, missing content, or per-unit failures are skipped according to the existing endpoint behavior. The intended runtime accounting change is internal: in-flight copy capacity appears as reserved quota instead of already-used quota.

**Rationale:** Reservation-style accounting aligns copy with the upload lifecycle model and keeps `storage_used` as committed logical ownership only. It narrows the drift window: content ref-count increments, copied rows, and used-storage commits succeed or roll back together, while compensation for skipped or failed work is a release of uncommitted reservation. This makes partial failure paths easier to audit than a pre-increment followed by later negative used-storage adjustments.

**Rejected alternatives:**

- Keep current pre-increment of `storage_used`: rejected because a failure between the upfront increment and later compensation can leave used-storage drift unrelated to committed file rows or content ref-counts.
- Increment `storage_used` only after each successful copy without an upfront reservation: rejected because concurrent writes could consume quota mid-copy and change the current all-candidate quota admission semantics into quota-driven partial success.
- Reserve only after all conflict/content preflight checks: deferred because it would intentionally change when quota rejection happens relative to existing skip behavior; a later implementation may choose this only if it documents the public behavior change.

**Compensation rules for the implementation follow-up:**

- Target-folder resolution and source discovery happen before reservation; failures there require no quota or ref-count compensation.
- If the upfront reservation fails, return the existing quota error shape and create no copied rows or ref-count changes.
- For name conflicts, invalid/missing content, copying a folder into itself or a descendant, and other pre-transaction skips, release the reserved bytes for that skipped unit.
- For content ref-count increment failure, copied row creation failure, item-count update failure, or reserved-to-used commit failure inside a copy transaction, roll back the transaction so ref-counts and copied rows return to their previous state, then release the reservation for that failed unit.
- For a successful copy unit, commit exactly the copied logical bytes from `storage_reserved` to `storage_used` in the same transaction as ref-count and row creation.
- If reservation release fails after a skipped or failed unit, stop rather than silently continuing; log the affected `user_id`, byte count, and reason, and rely on accounting reconciliation to surface orphaned reservations.
- Reconciliation should flag copy-reservation drift explicitly, because `storage_reserved` currently also represents in-progress upload tasks.

**Implementation impact:** Later copy-accounting work should add a copy-specific quota helper or reuse `QuotaService` with checked reservation/commit/release calls; update `test/services/FileServiceAtomicity_test.cpp` and `test/integration/test_safety_content_quota.py` coverage for reservation commit/release, partial failures, retry/idempotency expectations, and `storage_used` / `storage_reserved` reconciliation; and preserve the current `CopyResponse` fields.

**Follow-up status:** Decision recorded; behavior-changing implementation remains open.

## Decision: trash counts against quota until permanent deletion or expiry cleanup

**Current implementation behavior:** `docs/backend-discovery.md` records that move-to-trash does not decrement `file_contents.ref_count` and does not decrease `users.storage_used`; expired trash cleanup later decrements content refs, deletes trash rows, and applies used-storage decrements.

**Accepted target behavior:** Recoverable trash remains user-owned content and continues counting against quota. Storage is released only when trash state is permanently deleted by the user or removed by expiry cleanup.

**Rationale:** This preserves current behavior and avoids restore-time ambiguity. A user can restore trash without needing to re-reserve quota or handle restore failure caused by quota consumed after deletion.

**Rejected alternatives:**

- Release quota on soft delete: rejected because recoverable content would become free quota and restore could require new quota checks or fail unpredictably.
- Split active quota and trash quota in `storage_used`: rejected for now because the current model only needs one logical usage number.

**Implementation impact:** Later trash lifecycle and cleanup work should preserve this rule, verify it with tests, and ensure quota release happens only during permanent deletion or equivalent expiry cleanup.

**Follow-up status:** Decision recorded; current behavior already matches the target, but test coverage and lifecycle consolidation remain open.

## Decision: private downloads update file-level metadata

**Current implementation behavior:** `docs/backend-discovery.md` records that private download and private download info read file/content metadata but do not update `files.download_count` or `files.last_accessed_at`.

**Accepted target behavior:** A successful private content download updates file-level metadata by incrementing `files.download_count` and refreshing `files.last_accessed_at`. Download-info requests remain metadata lookups and should not count as downloads.

**Rationale:** File-level metadata should reflect actual content access. If content downloads never update these fields, UI and audit views based on them become misleading.

**Rejected alternatives:**

- Preserve current no-update behavior: rejected because metadata fields would not represent real usage.
- Count download-info requests: rejected because they do not transfer file content and can be used by clients for planning or resume checks.

**Implementation impact:** Later file download work should update metadata only after the download request has been accepted for content transfer, preserve JSON error behavior for failures, and add tests for private download versus download-info behavior.

**Follow-up status:** Decision recorded; behavior-changing implementation remains open.

## Decision: share downloads update share-level and file-level metadata

**Current implementation behavior:** `docs/backend-discovery.md` records that share content downloads increment `shares.download_count`, but do not update `files.download_count` or `files.last_accessed_at`. Share download-info requests do not increment the share count.

**Accepted target behavior:** A successful shared content download updates both share-level download count and file-level metadata. Share download-info requests remain metadata lookups and should not count as downloads.

**Rationale:** Share-level metrics and file-level metrics answer different questions. Keeping both updated preserves share analytics while making file metadata reflect all successful content downloads.

**Rejected alternatives:**

- Share-level only: rejected because file-level metadata would undercount content access.
- File-level only: rejected because share usage analytics already exist and should be preserved.
- Count share download-info requests: rejected because metadata lookup is not content transfer.

**Implementation impact:** Later share download work should preserve `shares.download_count` behavior, add file metadata updates for successful content download responses, and test that share download-info does not increment either content-download counter.

**Follow-up status:** Decision recorded; behavior-changing implementation remains open.

## Decision: JWT enforcement uses global-with-exemptions

**Current implementation behavior:** `docs/backend-discovery.md` records that protected routes can match both global `JwtAuthFilter` and route-level `JwtAuthFilter`, causing duplicate JWT validation for representative file, upload, folder, share-owner, auth logout, and admin routes.

**Accepted target behavior:** JWT authentication is owned by global filter configuration with explicit public exemptions. Public auth, health, and public share routes remain exempt. Protected upload, file, folder, share-owner, auth logout, and admin routes should not also declare route-level JWT when global JWT already covers them.

**Rationale:** Global-with-exemptions is default-secure, reduces per-route drift, and removes duplicate JWT execution. It also keeps the public route list visible in one configuration surface.

**Rejected alternatives:**

- Route-level-only JWT: rejected because every protected route must be individually audited and future route additions can drift.
- Keep both global and route-level JWT: rejected because duplicate execution wastes work and makes filter behavior harder to reason about.

**Implementation impact:** Later filter cleanup should remove duplicate route-level JWT declarations for globally protected routes, preserve public exemptions, preserve admin/share-specific authorization filters where still needed, and add tests proving JWT executes exactly once for representative protected routes.

**Follow-up status:** Decision recorded; filter cleanup implementation remains open.

## Decision: Redis rate-limit failures remain fail-open for now

**Current implementation behavior:** `docs/backend-discovery.md` records that upload, download, register, public share, admin, and folder rate-limit filters log Redis errors and allow the request to continue.

**Accepted target behavior:** All current rate-limit families remain fail-open when Redis increment/check operations fail. This is a temporary explicit policy, not an accidental implementation detail.

**Rationale:** A Redis outage should not broadly take down core file-storage flows by default. Keeping fail-open preserves current availability behavior while future abuse-driven changes can evaluate endpoint-specific fail-closed policies separately.

**Rejected alternatives:**

- Fail-closed for every rate-limit family: rejected because Redis would become a hard dependency for broad API availability.
- Fail-closed only for selected sensitive endpoints now: deferred because no endpoint-specific risk decision has been made in this change.

**Implementation impact:** Later rate-limit work should keep fail-open behavior explicit in filter code, logs, and tests. Rate-limit rejection responses should keep consistent headers when Redis is available and the request is actually limited.

**Follow-up status:** Decision recorded; current behavior already matches the target, but explicit code/test documentation remains open.

## Remaining Unresolved Decisions

The following roadmap questions remain outside this decision set:

- Whether object storage compatibility is a near-term requirement or only a design constraint.
- Whether any specific rate-limit family should become fail-closed in a future abuse or security hardening change.
