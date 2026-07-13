# Backend Refactor Decisions

This note records accepted product and policy decisions for the backend refactor roadmap. It is intentionally documentation-only: it does not mean the runtime behavior has already changed.

Use `docs/backend-discovery.md` as the source of confirmed current implementation behavior. When this note differs from discovery, discovery describes the current behavior and this note describes the accepted target for a later implementation change.

## Decision Summary

| Area | Accepted decision | Current implementation status | Follow-up status |
| --- | --- | --- | --- |
| `storage_used` meaning | Logical per-user bytes | Logical per-user accounting is implemented for upload, instant upload, copy, trash, and download-side metadata behavior | Copy reservation-style accounting remains a separate open design question tracked by issue #21 |
| Instant upload accounting | Increase `storage_used` like copy | Implemented | Closed |
| Trash quota | Trash counts against quota until permanent delete / expiry cleanup | Implemented and covered by lifecycle cleanup work | Closed |
| Private download metadata | Successful private content download updates file metadata | Implemented | Closed |
| Share download metadata | Successful share content download updates share-level and file-level metadata | Implemented | Closed |
| JWT enforcement | Global JWT with explicit public exemptions | Implemented with public exemptions and route-level duplicate removal | Closed |
| Redis rate-limit failure | Fail-open for now | Implemented explicitly for current rate-limit families | Closed for current policy; future fail-closed hardening would require a new decision |
| Object storage compatibility | Design constraint for now, not a near-term implementation target | Local filesystem implementation with explicit staging, blob, and download descriptor boundaries | Runtime S3/MinIO support remains deferred |

## Decision: `storage_used` means logical per-user bytes

**Current implementation behavior:** `docs/backend-discovery.md` records mixed semantics. Upload completion increases `users.storage_used`, copy pre-check/reservation increases `users.storage_used`, but instant upload reusing existing content does not increase `users.storage_used`.

**Accepted target behavior:** `users.storage_used` means logical bytes charged to a user-owned namespace, not globally unique physical bytes stored in `file_contents`. Deduplication remains an internal storage optimization and does not reduce the user's logical quota usage.

**Rationale:** Logical per-user bytes match user expectations and current copy behavior. Users see files in their namespace and quota should reflect that visible ownership rather than hidden physical deduplication state.

**Rejected alternatives:**

- Physical unique bytes: rejected because quota would depend on global content reuse and would conflict with existing copy accounting.
- Hybrid per-operation accounting: rejected because upload, instant upload, copy, and trash would be difficult to explain and test consistently.

**Implementation impact:** Later accounting work should make quota checks and `storage_used` mutations consistently use logical per-user bytes. Reconciliation tooling should compare stored usage against logical active-plus-trash file sizes rather than physical unique content size.

**Follow-up status:** Logical accounting cleanup has been implemented for the accepted current flows. Whether copy should move from its current pre-increment behavior to a reservation-style model remains a separate open design question tracked by issue #21.

## Decision: instant upload increases `storage_used`

**Current implementation behavior:** Instant upload increments `file_contents.ref_count`, inserts a `files` row, and increases `users.storage_used` consistently with logical per-user accounting.

**Accepted target behavior:** Instant upload creates a new logical file reference and must increase `users.storage_used` by the logical file size, subject to quota checks, consistently with copy semantics.

**Rationale:** Without this rule, users can bypass logical quota when content already exists physically. Aligning instant upload with copy makes quota behavior independent of deduplication hits.

**Rejected alternatives:**

- Preserve current no-increase behavior: rejected because it conflicts with logical per-user bytes and copy behavior.
- Defer quota checks until later file activity: rejected because quota enforcement should occur when the new file reference is created.

**Implementation impact:** Upload lifecycle work now checks available logical quota, increments used storage atomically with file row creation and content ref-count increment, and covers the behavior with regression tests.

**Follow-up status:** Implemented and closed.

## Decision: trash counts against quota until permanent deletion or expiry cleanup

**Current implementation behavior:** `docs/backend-discovery.md` records that move-to-trash does not decrement `file_contents.ref_count` and does not decrease `users.storage_used`; expired trash cleanup later decrements content refs, deletes trash rows, and applies used-storage decrements.

**Accepted target behavior:** Recoverable trash remains user-owned content and continues counting against quota. Storage is released only when trash state is permanently deleted by the user or removed by expiry cleanup.

**Rationale:** This preserves current behavior and avoids restore-time ambiguity. A user can restore trash without needing to re-reserve quota or handle restore failure caused by quota consumed after deletion.

**Rejected alternatives:**

- Release quota on soft delete: rejected because recoverable content would become free quota and restore could require new quota checks or fail unpredictably.
- Split active quota and trash quota in `storage_used`: rejected for now because the current model only needs one logical usage number.

**Implementation impact:** Later trash lifecycle and cleanup work should preserve this rule, verify it with tests, and ensure quota release happens only during permanent deletion or equivalent expiry cleanup.

**Follow-up status:** Implemented and covered by lifecycle cleanup work.

## Decision: private downloads update file-level metadata

**Current implementation behavior:** Private content downloads update `files.download_count` and `files.last_accessed_at`; private download-info requests remain metadata lookups and do not update those fields.

**Accepted target behavior:** A successful private content download updates file-level metadata by incrementing `files.download_count` and refreshing `files.last_accessed_at`. Download-info requests remain metadata lookups and should not count as downloads.

**Rationale:** File-level metadata should reflect actual content access. If content downloads never update these fields, UI and audit views based on them become misleading.

**Rejected alternatives:**

- Preserve current no-update behavior: rejected because metadata fields would not represent real usage.
- Count download-info requests: rejected because they do not transfer file content and can be used by clients for planning or resume checks.

**Implementation impact:** File download work updates metadata only after the download request has been accepted for content transfer, preserves JSON error behavior for failures, and tests private content download versus download-info behavior.

**Follow-up status:** Implemented and closed.

## Decision: share downloads update share-level and file-level metadata

**Current implementation behavior:** Share content downloads increment `shares.download_count` and update `files.download_count` / `files.last_accessed_at`. Share download-info requests do not increment content-download counters.

**Accepted target behavior:** A successful shared content download updates both share-level download count and file-level metadata. Share download-info requests remain metadata lookups and should not count as downloads.

**Rationale:** Share-level metrics and file-level metrics answer different questions. Keeping both updated preserves share analytics while making file metadata reflect all successful content downloads.

**Rejected alternatives:**

- Share-level only: rejected because file-level metadata would undercount content access.
- File-level only: rejected because share usage analytics already exist and should be preserved.
- Count share download-info requests: rejected because metadata lookup is not content transfer.

**Implementation impact:** Share download work preserves `shares.download_count`, updates file metadata for successful content download responses, and tests that share download-info does not increment content-download counters.

**Follow-up status:** Implemented and closed.

## Decision: JWT enforcement uses global-with-exemptions

**Current implementation behavior:** Protected routes are covered by global `JwtAuthFilter` with explicit public exemptions; duplicate route-level JWT declarations for globally protected routes have been removed.

**Accepted target behavior:** JWT authentication is owned by global filter configuration with explicit public exemptions. Public auth, health, and public share routes remain exempt. Protected upload, file, folder, share-owner, auth logout, and admin routes should not also declare route-level JWT when global JWT already covers them.

**Rationale:** Global-with-exemptions is default-secure, reduces per-route drift, and removes duplicate JWT execution. It also keeps the public route list visible in one configuration surface.

**Rejected alternatives:**

- Route-level-only JWT: rejected because every protected route must be individually audited and future route additions can drift.
- Keep both global and route-level JWT: rejected because duplicate execution wastes work and makes filter behavior harder to reason about.

**Implementation impact:** Filter cleanup removed duplicate route-level JWT declarations for globally protected routes, preserves public exemptions, preserves admin/share-specific authorization filters where still needed, and tests representative protected-route behavior.

**Follow-up status:** Implemented and closed.

## Decision: Redis rate-limit failures remain fail-open for now

**Current implementation behavior:** `docs/backend-discovery.md` records that upload, download, register, public share, admin, and folder rate-limit filters log Redis errors and allow the request to continue.

**Accepted target behavior:** All current rate-limit families remain fail-open when Redis increment/check operations fail. This is a temporary explicit policy, not an accidental implementation detail.

**Rationale:** A Redis outage should not broadly take down core file-storage flows by default. Keeping fail-open preserves current availability behavior while future abuse-driven changes can evaluate endpoint-specific fail-closed policies separately.

**Rejected alternatives:**

- Fail-closed for every rate-limit family: rejected because Redis would become a hard dependency for broad API availability.
- Fail-closed only for selected sensitive endpoints now: deferred because no endpoint-specific risk decision has been made in this change.

**Implementation impact:** Rate-limit work keeps fail-open behavior explicit in filter code, logs, and tests. Rate-limit rejection responses keep consistent headers when Redis is available and the request is actually limited.

**Follow-up status:** Implemented and closed for current rate-limit families. Any future fail-closed policy requires a new abuse/security hardening decision.

## Decision: object storage compatibility is a design constraint for now

**Current implementation behavior:** The runtime implementation remains local-filesystem based. `UploadStagingStorage` separates temporary upload-session concerns from final blob storage concerns, `BlobStore` separates final content blob promotion, read, existence, size, and deletion semantics, and download responses use descriptor-oriented blob contracts instead of controller-local filesystem assumptions.

**Accepted target behavior:** S3/MinIO compatibility is a Phase 6 design constraint, not a near-term implementation requirement. New storage boundaries should avoid relying on local-only primitives where an object store cannot provide the same guarantee, but this roadmap does not require shipping an S3/MinIO adapter until the remaining download descriptor contracts are explicit.

**Expected staging-storage responsibilities for S3/MinIO:** An object-store implementation of `UploadStagingStorage` should store chunks under an upload-session namespace, make repeated chunk writes idempotent for the same upload id and chunk index, assemble chunks into a staging object or multipart upload result, calculate and return MD5/SHA-256 metadata for final validation, discard assembled staging artifacts on validation or DB failure, and clean all per-upload staging keys during cancel/expiry cleanup. It should not create final content blobs, mutate database state, or assume local directory rename semantics.

**Expected final blob-storage responsibilities for S3/MinIO:** `BlobStore` should own content-addressed final blobs, promotion from a staging descriptor/object into the final hash-derived key, existence checks, metadata such as size/checksum where needed, readable download descriptors instead of local filesystem paths, and idempotent deletion requests. Final keys should remain derived from content hashes to preserve dedup semantics, but callers should not depend on POSIX paths or atomic filesystem rename.

**Consistency and compensation model:** Database transactions and object-store side effects remain separate failure domains. Blob promotion should occur outside the DB transaction unless a later design introduces an explicit outbox/saga worker. If object-store promotion succeeds and the DB transaction fails, the caller must issue best-effort compensation to delete the promoted final blob. If DB commit succeeds but cleanup of staging artifacts fails, the committed file remains valid and cleanup must be retried asynchronously or by scheduled maintenance. Delete paths must keep zero-ref verification in the DB before object-store deletion, and object-store delete operations should be idempotent because retries may observe already-deleted keys.

**Rejected alternatives:**

- Make S3/MinIO a near-term implementation target now: rejected because download descriptor contracts are still open, so an adapter would either leak local filesystem assumptions or be reworked soon.
- Keep object storage entirely out of the design: rejected because Phase 6 is explicitly extracting storage boundaries, and local-only contracts would make a later S3/MinIO adapter unnecessarily risky.
- Rely on atomic rename semantics for promotion: rejected because object stores generally expose copy/complete/delete operations rather than POSIX rename.

**Implementation impact:** Phase 6 storage/download boundaries keep DB failure compensation explicit around object-store promotion/deletion, move download responses toward blob descriptors so controllers do not need local path knowledge, and leave S3/MinIO adapter implementation deferred. `docs/TODO.md` Phase 6.5 records this documentation task as closed while leaving runtime S3/MinIO support outside the current roadmap.

**Follow-up status:** Decision recorded; runtime S3/MinIO support is deferred and should be tracked as a separate feature if it becomes a near-term target.

## Remaining Unresolved Decisions

The following roadmap question remains outside this decision set:

- Whether copy accounting should move to a reservation-style model instead of pre-incrementing `storage_used` (tracked by issue #21).

A future abuse or security hardening pass may separately revisit whether any specific rate-limit family should become fail-closed, but that is not an open backend-refactor roadmap item.
