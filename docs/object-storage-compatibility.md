# Object Storage Compatibility

This note records the storage-abstraction constraints for a future S3/MinIO backend. It is documentation-only: the current runtime remains local filesystem based, and this task does not implement an object-store backend or change public API behavior.

## Decision

Object storage compatibility is a current architecture design constraint, not a near-term runtime requirement. Phase 6 storage-abstraction work should avoid adding new local filesystem path assumptions, but S3/MinIO support should be implemented only after the `BlobStore` boundary and download descriptor work are explicit.

The current public behavior must remain unchanged while preparing for that future backend:

- Existing upload, download, trash, quota, ref-count, and cache side effects are preserved.
- Public API response envelopes and JSON field shapes are unchanged.
- Private and shared range download behavior is preserved.
- Local `LocalFileStorage` / local blob behavior is not migrated by this note.

## Object key model

A future object-store backend should use deterministic, non-user-controlled object keys and treat them as opaque storage descriptors rather than filesystem paths.

Suggested prefixes:

```text
staging/uploads/{upload_id}/chunks/{chunk_index}
staging/uploads/{upload_id}/assembly/{attempt_id}
blobs/{md5_prefix}/{md5}.bin
```

The final blob key intentionally mirrors the current local layout shape (`{md5_prefix}/{md5}.bin`) so content-addressed deduplication remains stable. Database rows should store a logical storage key or descriptor, not an absolute local path. Controllers and services should not parse object keys for product behavior.

## Upload staging on S3/MinIO

`UploadStagingStorage` maps naturally to object storage if every operation is idempotent and keyed by `upload_id`.

| Operation | Object-store behavior | Idempotency / retry rule |
| --- | --- | --- |
| `EnsureUploadTempDir(upload_id)` | Usually a no-op. Optionally write a small marker object under `staging/uploads/{upload_id}/` if lifecycle tooling requires it. | Re-running must be safe. Do not rely on directory semantics. |
| `WriteChunk(upload_id, chunk_index, data)` | `PutObject` to `staging/uploads/{upload_id}/chunks/{chunk_index}`. Attach checksum/size metadata where available. | Retrying the same chunk overwrites the same key. DB chunk insertion remains `ON CONFLICT DO NOTHING`; checksum validation still happens before or during write. |
| `AssembleChunks(upload_id, chunk_count)` | Create an assembled staging object under `staging/uploads/{upload_id}/assembly/{attempt_id}` while streaming chunks in index order and computing MD5/SHA-256. S3 cannot append to an object, so large assemblies should use multipart upload or provider-specific compose/copy primitives. | On failure, abort multipart upload and delete the partial assembled object. Missing chunk objects are hard failures, matching current local behavior. |
| `DiscardAssembly(upload_id, assembly)` | Delete the assembled staging object described by the assembly descriptor. | Missing object is success. Validate the descriptor belongs to the same `upload_id` before deleting. |
| `CleanupTemp(upload_id)` | Bulk-delete all known chunk and assembly keys under the upload prefix; abort lingering multipart uploads for the prefix. | Deletes are idempotent. Listing is acceptable for cleanup, but not for correctness-critical upload completion decisions. |

Compatibility constraints:

- Do not model staging as local directories; prefixes are only key conventions.
- Do not require atomic rename. Object stores promote by copy/put, not rename.
- Do not depend on listing to prove all chunks exist; upload completion should keep using DB chunk coverage and direct `HEAD`/`GET` of expected chunk keys when assembling.
- Cleanup failures should be logged and retried by scheduled cleanup/reconciliation; they should not change successful API response shapes.

## Final blob storage on S3/MinIO

A future `BlobStore` boundary should own final content blobs independently from upload staging. It should expose operations in terms of descriptors such as key, size, checksum, ETag/version, and range-read capability.

| Capability | Object-store behavior | Required invariant |
| --- | --- | --- |
| Promote final blob | Copy or multipart-copy the assembled staging object to `blobs/{md5_prefix}/{md5}.bin` after verifying the assembled hash. If the content key already exists, reuse it and delete the staging assembly. | Promotion must be content-address idempotent. Races between identical uploads should converge on one final key and one DB content row. |
| Read/download descriptor | Return an opaque descriptor that supports size lookup and byte-range reads. The controller may stream `GetObject` ranges or use an internal responder abstraction; it must not expose bucket/key details in API JSON. | Preserve current `Range` / `206 Partial Content` behavior and existing metadata side effects for successful private/share downloads. |
| Delete final blob | Delete `blobs/{md5_prefix}/{md5}.bin` only after DB zero-ref verification. Treat missing object as success. | Ref-count and quota/trash semantics remain DB-owned; blob deletion is a side effect with compensation/retry. |
| Size/checksum lookup | Prefer DB size/hash as the product source of truth; use `HEAD` for storage verification and diagnostics. | Do not make user-visible metadata depend on eventually incomplete list results. |

Large files require multipart-aware implementation details:

- S3 single `PutObject` and single `CopyObject` are limited; large assembled objects need multipart upload / multipart copy.
- `UploadPartCopy` can promote ranges from a staging object to the final object for very large blobs.
- MinIO supports S3-compatible multipart APIs and may also expose compose helpers, but the backend should rely on portable S3 semantics unless a provider-specific adapter is introduced.
- Multipart uploads must be aborted on failure and covered by lifecycle cleanup for abandoned upload IDs.

## DB transaction and object-store consistency

Database commits and object-store writes cannot share one atomic transaction. The architecture should keep the current explicit-compensation style and make every side effect safe to retry.

### Upload completion for new content

Recommended order:

1. Assemble staging object and verify final MD5/SHA-256 before opening the DB transaction.
2. Promote/copy the assembled object to the final blob key outside the DB transaction.
3. Run one DB transaction for content row creation, file row creation, quota reservation commit, upload task completion, and chunk-row cleanup.
4. If the DB transaction fails after a new final blob was created, delete the final blob as compensation.
5. Clean staging objects in a best-effort idempotent step after completion; retry later if cleanup fails.

Failure handling:

- Promotion succeeds, DB fails, compensation delete succeeds: return the DB error; no durable blob leak remains.
- Promotion succeeds, DB fails, compensation delete fails: return the DB error and record enough information for orphan cleanup (`blob_key`, hash, upload_id, failure time).
- DB commits, staging cleanup fails: the user-visible upload remains successful; scheduled cleanup removes stale staging objects.
- DB commits, final blob later appears missing: reconciliation should flag this as storage corruption because DB is the source of visible content ownership.

### Upload completion reusing existing content

When an existing content row is reused, no final blob promotion is needed. The DB transaction should increment the existing content ref-count, insert the file row, commit quota reservation, mark the upload completed, and clean chunk rows. The assembled staging object should be discarded idempotently; discard failure is a cleanup retry concern, not a content ownership change.

### Permanent deletion and trash cleanup

Trash and permanent-delete flows should keep their current ownership rule: moving to trash does not release quota or decrement `file_contents.ref_count`; permanent deletion or expiry cleanup does. For object storage:

1. DB logic verifies that the content ref-count reaches zero and commits the logical deletion/quota release.
2. Blob deletion runs as an object-store side effect after zero-ref verification.
3. `DeleteObject` treats missing keys as success.
4. If blob deletion fails after DB state is committed, record a retry/orphan-cleanup item instead of silently losing the side effect.

This preserves restore semantics and avoids deleting a shared physical blob while another logical file still references it.

## Consistency assumptions

Modern S3 has strong read-after-write consistency for puts and deletes, and MinIO is generally strongly consistent in typical deployments. The design should still avoid relying on provider-specific timing for correctness:

- Use deterministic keys plus `HEAD`/`GET` for objects that should exist.
- Use DB state for upload chunk coverage, ref-counts, quota, trash ownership, and user-visible file metadata.
- Treat object listing as a cleanup/reconciliation aid only.
- Make all cleanup/delete operations idempotent and safe to repeat.
- Prefer explicit descriptors over `std::filesystem::path` in future `BlobStore` and download-responder APIs.

## Reconciliation jobs to add with an object-store backend

Before enabling S3/MinIO in production, add scheduled or admin-triggered reconciliation for:

- Stale staging prefixes for expired, cancelled, or completed uploads.
- Abandoned multipart uploads under staging and blob prefixes.
- Final blobs with no matching `file_contents` row after a grace period.
- `file_contents` rows whose final blob key is missing or whose size/hash does not match storage metadata.
- Failed post-commit blob deletes from permanent-delete or trash-expiry flows.

These jobs should emit structured logs and metrics, use bounded batches, and keep retry state outside request/response paths.

## Follow-up implementation split

Recommended later work should remain separated:

1. Define `BlobStore` and blob descriptors without changing local behavior.
2. Move final promotion and deletion from `IFileStorage`-style filesystem paths to `BlobStore` operations.
3. Update the download responder to stream from blob descriptors while preserving range behavior.
4. Add object-store reconciliation/outbox primitives.
5. Implement an S3/MinIO staging adapter and blob adapter behind configuration.
6. Add multipart/copy-specific tests for large files and provider failure compensation.
