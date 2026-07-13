# Backend Refactor Decisions

This note records accepted product and policy decisions for the backend refactor roadmap and tracks whether the follow-up implementation has caught up with those decisions.

Use `docs/backend-discovery.md` as the source of the confirmed behavior baseline discovered for the backend refactor. When this note differs from discovery, discovery describes that baseline and this note records the accepted decision plus any later implementation status from `docs/TODO.md`.

## Decision Summary

| Area | Accepted decision | Current implementation status | Follow-up status |
| --- | --- | --- | --- |
| `storage_used` meaning | Logical per-user bytes | Upload completion, copy, instant upload, and trash lifecycle work now follow the logical per-user quota rule; copy still uses the current pre-increment accounting model | Product decision closed; copy reservation model remains a separate open architecture question |
| Instant upload accounting | Increase `storage_used` like copy | Implemented through upload lifecycle/quota work | Closed |
| Trash quota | Trash counts against quota until permanent delete / expiry cleanup | Implemented and preserved through trash lifecycle and transaction-boundary work | Closed |
| Private download metadata | Successful private content download updates file metadata | Implemented for content downloads; download-info remains metadata-only | Closed |
| Share download metadata | Successful share content download updates share-level and file-level metadata | Implemented for content downloads; share download-info remains metadata-only | Closed |
| JWT enforcement | Global JWT with explicit public exemptions | Implemented; duplicate route-level JWT declarations were removed while public exemptions were preserved | Closed |
| Redis rate-limit failure | Fail-open for all current rate-limit families for now | Implemented explicitly in code/tests | Closed for current rate-limit families |
| Inline expired upload cleanup | Expire through upload lifecycle/quota boundary and release reserved quota | Implemented through `UploadLifecycleService` / `QuotaService`; temp cleanup remains idempotent | Closed |
| Upload staging storage | Temporary upload sessions, chunk writes, assembly, and temp cleanup use an explicit staging boundary | `UploadStagingStorage` is defined and upload assembly has moved to staging storage | Staging portion closed; final `BlobStore`, download descriptors, and object-store compatibility remain Phase 6 work |

## Decision: `storage_used` means logical per-user bytes

**Baseline behavior:** `docs/backend-discovery.md` recorded mixed semantics. Upload completion increased `users.storage_used`, copy pre-check/reservation increased `users.storage_used`, but instant upload reusing existing content did not increase `users.storage_used`.

**Accepted target behavior:** `users.storage_used` means logical bytes charged to a user-owned namespace, not globally unique physical bytes stored in `file_contents`. Deduplication remains an internal storage optimization and does not reduce the user's logical quota usage.

**Current implementation status:** The behavior-changing follow-ups for instant upload, copy/trash preservation, and download metadata have been completed in the roadmap. `storage_used` now represents logical per-user bytes for the implemented upload/copy/trash paths. The remaining architecture question is whether copy should continue pre-incrementing `storage_used` or move to a reservation-style model.

**Rationale:** Logical per-user bytes match user expectations and current copy behavior. Users see files in their namespace and quota should reflect that visible ownership rather than hidden physical deduplication state.

**Rejected alternatives:**

- Physical unique bytes: rejected because quota would depend on global content reuse and would conflict with existing copy accounting.
- Hybrid per-operation accounting: rejected because upload, instant upload, copy, and trash would be difficult to explain and test consistently.

**Implementation impact:** Future accounting work should preserve logical per-user semantics. Reconciliation tooling should compare stored usage against logical active-plus-trash file sizes rather than physical unique content size. Any copy-accounting reservation change should be treated as an implementation model change, not a change to the logical quota rule.

**Follow-up status:** Product decision closed; implementation is complete for the known behavior-changing paths. Copy reservation-style accounting remains open separately.

## Decision: instant upload increases `storage_used`

**Baseline behavior:** `docs/backend-discovery.md` recorded that instant upload incremented `file_contents.ref_count` and inserted a `files` row, but did not increase `users.storage_used`.

**Accepted target behavior:** Instant upload creates a new logical file reference and must increase `users.storage_used` by the logical file size, subject to quota checks, consistently with copy semantics.

**Current implementation status:** Implemented. Instant upload quota checks and `storage_used` mutation have been updated to match logical per-user accounting.

**Rationale:** Without this rule, users can bypass logical quota when content already exists physically. Aligning instant upload with copy makes quota behavior independent of deduplication hits.

**Rejected alternatives:**

- Preserve the no-increase behavior: rejected because it conflicts with logical per-user bytes and copy behavior.
- Defer quota checks until later file activity: rejected because quota enforcement should occur when the new file reference is created.

**Implementation impact:** Keep instant-upload file creation, content ref-count increment, and quota mutation aligned in the upload lifecycle boundary. Tests should continue covering instant-upload quota behavior as a logical file creation path.

**Follow-up status:** Closed.

## Decision: trash counts against quota until permanent deletion or expiry cleanup

**Baseline behavior:** `docs/backend-discovery.md` recorded that move-to-trash did not decrement `file_contents.ref_count` and did not decrease `users.storage_used`; expired trash cleanup later decremented content refs, deleted trash rows, and applied used-storage decrements.

**Accepted target behavior:** Recoverable trash remains user-owned content and continues counting against quota. Storage is released only when trash state is permanently deleted by the user or removed by expiry cleanup.

**Current implementation status:** Implemented and preserved. Trash lifecycle extraction and delete/trash transaction-boundary work keep quota release at permanent deletion or equivalent expiry cleanup, not at soft-delete time.

**Rationale:** This preserves current behavior and avoids restore-time ambiguity. A user can restore trash without needing to re-reserve quota or handle restore failure caused by quota consumed after deletion.

**Rejected alternatives:**

- Release quota on soft delete: rejected because recoverable content would become free quota and restore could require new quota checks or fail unpredictably.
- Split active quota and trash quota in `storage_used`: rejected for now because the current model only needs one logical usage number.

**Implementation impact:** Future trash work should preserve this rule, verify it with tests, and ensure quota release happens only during permanent deletion or equivalent expiry cleanup.

**Follow-up status:** Closed.

## Decision: private downloads update file-level metadata

**Baseline behavior:** `docs/backend-discovery.md` recorded that private download and private download info read file/content metadata but did not update `files.download_count` or `files.last_accessed_at`.

**Accepted target behavior:** A successful private content download updates file-level metadata by incrementing `files.download_count` and refreshing `files.last_accessed_at`. Download-info requests remain metadata lookups and should not count as downloads.

**Current implementation status:** Implemented. Successful private content downloads update file-level metadata; download-info requests remain metadata-only.

**Rationale:** File-level metadata should reflect actual content access. If content downloads never update these fields, UI and audit views based on them become misleading.

**Rejected alternatives:**

- Preserve no-update behavior: rejected because metadata fields would not represent real usage.
- Count download-info requests: rejected because they do not transfer file content and can be used by clients for planning or resume checks.

**Implementation impact:** Keep metadata updates tied to accepted content-transfer requests and preserve JSON error behavior for failures. Tests should continue distinguishing private content download from download-info behavior.

**Follow-up status:** Closed.

## Decision: share downloads update share-level and file-level metadata

**Baseline behavior:** `docs/backend-discovery.md` recorded that share content downloads incremented `shares.download_count`, but did not update `files.download_count` or `files.last_accessed_at`. Share download-info requests did not increment the share count.

**Accepted target behavior:** A successful shared content download updates both share-level download count and file-level metadata. Share download-info requests remain metadata lookups and should not count as downloads.

**Current implementation status:** Implemented. Successful share content downloads preserve share-level counting and also update file-level metadata; share download-info remains metadata-only.

**Rationale:** Share-level metrics and file-level metrics answer different questions. Keeping both updated preserves share analytics while making file metadata reflect all successful content downloads.

**Rejected alternatives:**

- Share-level only: rejected because file-level metadata would undercount content access.
- File-level only: rejected because share usage analytics already exist and should be preserved.
- Count share download-info requests: rejected because metadata lookup is not content transfer.

**Implementation impact:** Keep `shares.download_count` behavior and file metadata updates tied to successful shared content downloads. Tests should continue proving share download-info does not increment content-download counters.

**Follow-up status:** Closed.

## Decision: JWT enforcement uses global-with-exemptions

**Baseline behavior:** `docs/backend-discovery.md` recorded that protected routes could match both global `JwtAuthFilter` and route-level `JwtAuthFilter`, causing duplicate JWT validation for representative file, upload, folder, share-owner, auth logout, and admin routes.

**Accepted target behavior:** JWT authentication is owned by global filter configuration with explicit public exemptions. Public auth, health, and public share routes remain exempt. Protected upload, file, folder, share-owner, auth logout, and admin routes should not also declare route-level JWT when global JWT already covers them.

**Current implementation status:** Implemented. The filter policy closure work centralizes JWT enforcement in the global-with-exemptions configuration and removes duplicate route-level JWT declarations while preserving protected route behavior.

**Rationale:** Global-with-exemptions is default-secure, reduces per-route drift, and removes duplicate JWT execution. It also keeps the public route list visible in one configuration surface.

**Rejected alternatives:**

- Route-level-only JWT: rejected because every protected route must be individually audited and future route additions can drift.
- Keep both global and route-level JWT: rejected because duplicate execution wastes work and makes filter behavior harder to reason about.

**Implementation impact:** Future route additions should rely on the global JWT policy unless they are intentionally public and covered by an explicit exemption. Admin/share-specific authorization filters remain separate from JWT identity validation where still needed.

**Follow-up status:** Closed.

## Decision: Redis rate-limit failures remain fail-open for now

**Baseline behavior:** `docs/backend-discovery.md` recorded that upload, download, register, public share, admin, and folder rate-limit filters logged Redis errors and allowed the request to continue.

**Accepted target behavior:** All current rate-limit families remain fail-open when Redis increment/check operations fail. This is a temporary explicit policy, not an accidental implementation detail.

**Current implementation status:** Implemented. The auth/rate-limit policy closure keeps fail-open Redis limiter behavior explicit in code and tests and normalizes limiter configuration lookup.

**Rationale:** A Redis outage should not broadly take down core file-storage flows by default. Keeping fail-open preserves current availability behavior while future abuse-driven changes can evaluate endpoint-specific fail-closed policies separately.

**Rejected alternatives:**

- Fail-closed for every rate-limit family: rejected because Redis would become a hard dependency for broad API availability.
- Fail-closed only for selected sensitive endpoints now: deferred because no endpoint-specific risk decision has been made in this change.

**Implementation impact:** Future rate-limit work should keep failure behavior explicit. Any endpoint-specific fail-closed policy should be proposed as a separate hardening decision with its expected response and availability tradeoff.

**Follow-up status:** Closed for current rate-limit families.

## Decision: inline expired upload cleanup releases reserved quota through lifecycle/quota boundary

**Baseline behavior:** `docs/backend-discovery.md` recorded that `InitUpload` could delete an expired resumable task and clean temp storage without visibly releasing `users.storage_reserved` in that inline path.

**Accepted target behavior:** Inline expired upload task cleanup during upload init should expire the task through the upload lifecycle boundary, release reserved quota through the quota boundary, and keep temporary cleanup idempotent.

**Current implementation status:** Implemented. `docs/TODO.md` records this as closed by `fix(backend): release quota for expired upload init cleanup`; inline expired-task handling now uses `UploadLifecycleService` / `QuotaService` and preserves idempotent temp cleanup.

**Rationale:** Expiration behavior should not differ depending on whether an expired task is found by scheduled cleanup or by upload init. Routing both paths through lifecycle/quota boundaries prevents reserved-quota drift.

**Rejected alternatives:**

- Delete the expired task inline without quota release: rejected because it can leave `storage_reserved` overstated.
- Duplicate quota-release SQL in the upload-init branch: rejected because lifecycle and scheduled cleanup paths could drift again.

**Implementation impact:** Keep upload-init cleanup, scheduled expiration, quota release, and temp cleanup behavior in shared lifecycle/quota primitives where practical.

**Follow-up status:** Closed.

## Phase 6 storage and object-store compatibility status

Storage abstraction work is partially complete and remains split between resolved implementation work and open architecture decisions:

- `UploadStagingStorage` is defined for temporary upload sessions, chunk writes, assembly, and temp cleanup.
- Upload assembly has moved to staging storage while preserving local filesystem compatibility and temp cleanup idempotency.
- Final content promotion/deletion still needs a `BlobStore` boundary.
- Download responders still need to move away from local filesystem assumptions where practical while preserving range behavior and existing side effects.
- S3/MinIO object storage compatibility is still an open architecture decision: near-term requirement versus design constraint.

## Remaining Unresolved Decisions

The following roadmap questions remain outside this decision set:

- Whether copy accounting should move to a reservation-style model instead of pre-incrementing `storage_used`.
- Whether object storage compatibility is a near-term requirement or only a design constraint.
