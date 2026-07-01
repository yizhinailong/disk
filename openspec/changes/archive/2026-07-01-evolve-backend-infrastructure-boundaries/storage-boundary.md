# Storage Boundary Vocabulary

This note documents the future split of the current `IFileStorage` abstraction. It does not introduce new runtime interfaces in this change.

## Intended boundaries

### UploadStagingStorage

`UploadStagingStorage` owns temporary upload-session state. Its vocabulary is upload/session/chunk oriented:

- upload session identity: `upload_id`
- chunk identity: `upload_id + chunk_index`
- temporary assembled object: result of assembling uploaded chunks before content registration
- cleanup scope: one upload session and its temporary artifacts

Current `IFileStorage` operations that belong primarily to this boundary:

- `EnsureUploadTempDir(upload_id)`
- `WriteChunk(upload_id, chunk_index, data)`
- `AssembleChunks(upload_id, chunk_count)`
- `CleanupTemp(upload_id)`

### BlobStore

`BlobStore` owns final deduplicated content objects. Its vocabulary is content/blob oriented:

- blob identity: content hash and/or a future blob descriptor
- blob location: currently `storage_path`, later possibly object-store bucket/key metadata
- blob read: owner/share download paths read final content blobs
- blob deletion: only after content reference checks verify zero references

Current `IFileStorage` operations that belong primarily to this boundary:

- `PromoteToFinal(temp_path, hash)`
- `OpenForRead(storage_path)`
- `GetFinalStoragePath(hash)`
- `GetFileSize(target_path)` for final blob metadata checks

### Shared or transitional operations

`DeletePath(target_path)` is transitional. It is currently used for both temporary upload artifacts and final blobs. A future split should replace it with narrower operations such as staging-session cleanup and blob deletion by descriptor/path, while preserving explicit zero-reference safety checks outside the storage abstraction.

`Exists(target_path)` is also transitional because callers may use it for either temporary paths or final blob paths.

## Compatibility constraint

The current local blob layout remains a compatibility constraint:

```text
build/uploaded/{md5_prefix}/{md5}.bin
```

A future `BlobStore` implementation should wrap this layout first rather than migrating stored data. S3/MinIO compatibility should be expressed through a descriptor such as bucket/key/etag/size, not by leaking local filesystem assumptions into service code.

## Deferral

This change intentionally defers new `UploadStagingStorage` and `BlobStore` interfaces until upload lifecycle and content registration boundaries are stable. The current implementation continues using `IFileStorage`.
