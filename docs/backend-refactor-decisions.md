# Backend Refactor Decisions

This note records accepted product and policy decisions together with their current implementation status for the completed backend refactor roadmap. The closed roadmap is archived at [`docs/archive/2026-07-14-backend-refactor-todo.md`](archive/2026-07-14-backend-refactor-todo.md); current work is tracked separately in [`docs/TODO.md`](TODO.md).

Use `docs/backend-discovery.md` as the historical discovery baseline. The status and current-behavior paragraphs in this note are authoritative for decisions implemented after that discovery pass.

## Decision Summary

| Area | Accepted decision | Current implementation status | Follow-up status |
| --- | --- | --- | --- |
| `storage_used` meaning | Logical per-user bytes | Logical per-user accounting is implemented for upload, instant upload, copy, trash, and download-side metadata behavior; copy now uses reservation-style commit/release internally | Closed |
| Instant upload accounting | Increase `storage_used` like copy | Implemented | Closed |
| Copy accounting | Reserve candidate logical bytes before copy work, then commit successful bytes to used storage and release skipped/failed bytes | Implemented | Closed |
| Trash quota | Trash counts against quota until permanent delete / expiry cleanup | Implemented and covered by lifecycle cleanup work | Closed |
| Private download metadata | Successful private content download updates file metadata | Implemented | Closed |
| Share download metadata | Successful share content download updates share-level and file-level metadata | Implemented | Closed |
| JWT enforcement | Global JWT with explicit public exemptions | Implemented with public exemptions and route-level duplicate removal | Closed |
| Redis rate-limit failure | Fail-open for now | Implemented explicitly for current rate-limit families | Closed for current policy; future fail-closed hardening would require a new decision |
| Object storage compatibility | S3/MinIO-compatible object storage is supported as a configurable runtime backend while local filesystem remains the default/supported backend | Implemented with explicit staging, blob, and download descriptor boundaries | Closed |

## Decision: `storage_used` means logical per-user bytes

**Current implementation behavior:** Logical per-user accounting is implemented for the known upload, instant upload, copy, trash, and download-side metadata paths. Copy now uses a reservation-style commit/release implementation model internally, preserving per-user logical quota semantics while avoiding pre-committed used-storage drift.

**Accepted target behavior:** `users.storage_used` means logical bytes charged to a user-owned namespace, not globally unique physical bytes stored in `file_contents`. Deduplication remains an internal storage optimization and does not reduce the user's logical quota usage.

**Rationale:** Logical per-user bytes match user expectations and current copy behavior. Users see files in their namespace and quota should reflect that visible ownership rather than hidden physical deduplication state.

**Rejected alternatives:**

- Physical unique bytes: rejected because quota would depend on global content reuse and would conflict with existing copy accounting.
- Hybrid per-operation accounting: rejected because upload, instant upload, copy, and trash would be difficult to explain and test consistently.

**Implementation impact:** Later accounting work should make quota checks and `storage_used` mutations consistently use logical per-user bytes. Reconciliation tooling should compare stored usage against logical active-plus-trash file sizes rather than physical unique content size.

**Follow-up status:** Logical accounting cleanup has been implemented for the accepted current flows, including copy reservation-style commit/release.

## Decision: instant upload increases `storage_used`

**Current implementation behavior:** Instant upload increments `file_contents.ref_count`, inserts a `files` row, and increases `users.storage_used` consistently with logical per-user accounting.

**Accepted target behavior:** Instant upload creates a new logical file reference and must increase `users.storage_used` by the logical file size, subject to quota checks, consistently with copy semantics.

**Rationale:** Without this rule, users can bypass logical quota when content already exists physically. Aligning instant upload with copy makes quota behavior independent of deduplication hits.

**Rejected alternatives:**

- Preserve current no-increase behavior: rejected because it conflicts with logical per-user bytes and copy behavior.
- Defer quota checks until later file activity: rejected because quota enforcement should occur when the new file reference is created.

**Implementation impact:** Upload lifecycle work now checks available logical quota, increments used storage atomically with file row creation and content ref-count increment, and covers the behavior with regression tests.

**Follow-up status:** Implemented and closed.

## Decision: copy accounting uses reservation-style commit/release

**Current implementation behavior:** `FileMutationService::Copy` reserves candidate copy bytes in `users.storage_reserved`, releases reserved bytes for skipped or failed copy work, and commits successful bytes from reserved to used storage in the same transaction as the copied rows and content ref-count changes.

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
- Checked reservation release and reserved-to-used commit must require `storage_reserved >= bytes`. Under-reservation is an accounting invariant violation: return an internal error and roll back instead of clamping the counter to zero while increasing `storage_used`.
- If reservation release fails after a skipped or failed unit, stop rather than silently continuing; log the affected `user_id`, byte count, and reason, and rely on accounting reconciliation to surface orphaned reservations.
- Reconciliation should flag copy-reservation drift explicitly, because `storage_reserved` currently also represents in-progress upload tasks.

**Implementation impact:** Copy-accounting work adds checked reservation/commit/release calls in `QuotaService`, updates copy flow accounting to avoid used-storage pre-increment drift, preserves the current `CopyResponse` fields, and expands safety coverage for reservation commit/release, partial failures, retry/idempotency expectations, and `storage_used` / `storage_reserved` reconciliation.

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

**Current implementation behavior:** Protected routes are covered by global `JwtAuthFilter` with explicit public exemptions; duplicate route-level JWT declarations for globally protected routes have been removed. Drogon's plugin registry keys plugins by name, so `config.json` declares exactly one `drogon::plugin::GlobalFilters` instance containing request tracing, JWT, and the self-scoped public rate limiters.

**Accepted target behavior:** JWT authentication is owned by global filter configuration with explicit public exemptions. Public auth, health, and public share routes remain exempt. Protected upload, file, folder, share-owner, auth logout, and admin routes should not also declare route-level JWT when global JWT already covers them.

**Rationale:** Global-with-exemptions is default-secure, reduces per-route drift, and removes duplicate JWT execution. It also keeps the public route list visible in one configuration surface.

**Rejected alternatives:**

- Route-level-only JWT: rejected because every protected route must be individually audited and future route additions can drift.
- Keep both global and route-level JWT: rejected because duplicate execution wastes work and makes filter behavior harder to reason about.

**Implementation impact:** Filter cleanup removed duplicate route-level JWT declarations for globally protected routes, preserves public exemptions, preserves admin/share-specific authorization filters where still needed, and tests representative protected-route behavior. Configuration tests also reject duplicate `GlobalFilters` plugin declarations because later duplicate configuration can prevent JWT execution entirely.

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

## Decision: object storage compatibility is supported as a configurable backend

**Current implementation behavior:** The runtime implementation supports both local filesystem storage and a configurable S3/MinIO-compatible final-blob backend. `UploadStagingStorage` separates temporary upload-session concerns from final blob storage concerns, `BlobStore` separates final content blob promotion, read, existence, size, and deletion semantics, and download responses use descriptor-oriented blob contracts instead of controller-local filesystem assumptions. The current S3 backend deliberately keeps upload chunks and assembled staging files on the local `temp_upload_path`; only final content blobs are object-store backed.

**Accepted target behavior:** S3/MinIO compatibility is implemented as a configurable storage backend while preserving the local filesystem backend. Storage boundaries should continue avoiding local-only primitives where an object store cannot provide the same guarantee, and new code should preserve the separation between upload staging, final blob storage, and database lifecycle decisions.

**Possible future S3-native staging responsibilities:** If a later product requirement introduces object-store-native upload staging, that work should store chunks under an upload-session namespace, make repeated chunk writes idempotent for the same upload id and chunk index, assemble chunks into a staging object or multipart upload result, calculate and return MD5/SHA-256 metadata for final validation, discard assembled staging artifacts on validation or DB failure, and clean all per-upload staging keys during cancel/expiry cleanup. It must be proposed as a separate issue/change rather than reopening completed Phase 6 work.

**Expected final blob-storage responsibilities for S3/MinIO:** `BlobStore` should own content-addressed final blobs, promotion from a staging descriptor/object into the final hash-derived key, existence checks, metadata such as size/checksum where needed, readable download descriptors instead of local filesystem paths, and idempotent deletion requests. Final keys should remain derived from content hashes to preserve dedup semantics, but callers should not depend on POSIX paths or atomic filesystem rename.

**Consistency and compensation model:** Database transactions and object-store side effects remain separate failure domains. Blob promotion should occur outside the DB transaction unless a later design introduces an explicit outbox/saga worker. If object-store promotion succeeds and the DB transaction fails, the caller must issue best-effort compensation to delete the promoted final blob. If DB commit succeeds but cleanup of staging artifacts fails, the committed file remains valid and cleanup must be retried asynchronously or by scheduled maintenance. Delete paths must keep zero-ref verification in the DB before object-store deletion. S3 deletion runs in one storage-worker task with at most three total attempts; every failed attempt logs bucket, object key, attempt number, error code, and message, while exhaustion returns the final error to the caller. Missing keys remain successful deletes, so compensation and repeated cleanup stay idempotent.

**Rejected alternatives:**

- Make S3/MinIO the only supported backend: rejected because local filesystem storage remains useful for development and simple deployments.
- Keep object storage entirely out of the design: rejected because Phase 6 is explicitly extracting storage boundaries, and local-only contracts would make a later S3/MinIO adapter unnecessarily risky.
- Rely on atomic rename semantics for promotion: rejected because object stores generally expose copy/complete/delete operations rather than POSIX rename.

**Implementation impact:** Phase 6 storage/download boundaries keep DB failure compensation explicit around object-store promotion/deletion, use blob descriptors so controllers do not need local path knowledge, and provide a configurable S3/MinIO-compatible final-blob backend with MinIO-oriented development support. AWS SDK for C++ remains a mandatory build dependency for the single backend binary, including local-only runtime configurations; making it optional would require a separate conditional-build design and is not an active refactor follow-up.

**Follow-up status:** Implemented and closed. S3-native upload staging is not an active requirement; create a separate issue/OpenSpec change if that requirement appears later.

## Remaining Unresolved Decisions

No backend-refactor roadmap product or architecture decisions are currently open.

A future abuse or security hardening pass may separately revisit whether any specific rate-limit family should become fail-closed, but that is not an open backend-refactor roadmap item.
