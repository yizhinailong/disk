## 1. File-list Query Object Foundation

- [x] 1.1 Identify the current file-list SQL branches, deterministic ordering helpers, pagination metadata, and row-to-response mapping in `FileQueryService`.
- [x] 1.2 Create a named `FileListQuery` boundary for file-list count queries, data queries, ordering, pagination, and row mapping.
- [x] 1.3 Update `FileQueryService::GetFileList` to delegate file-list database reads to `FileListQuery` while keeping orchestration and response behavior unchanged.
- [x] 1.4 Add or update tests that verify file-list results, pagination totals, ordering determinism, and all/file/folder type filtering remain unchanged.

## 2. File-list Cache Identity

- [x] 2.1 Define the normalized file-list query identity used for cache keys, including user, parent folder, type, sort field, sort order, page, and `page_size`.
- [x] 2.2 Extract or centralize file-list cache key construction so cache reads and writes use the same complete identity.
- [x] 2.3 Update file-list cache behavior to avoid collisions between requests that differ by `page_size`.
- [x] 2.4 Verify old Redis entries can expire naturally and no explicit migration is required.

## 3. Search Query Extraction

- [x] 3.1 Identify current search SQL, filtering, pagination, and row mapping behavior in `FileQueryService::Search`.
- [x] 3.2 Create a named `SearchQuery` boundary for search database reads and read-model mapping.
- [x] 3.3 Update `FileQueryService::Search` to delegate database reads to `SearchQuery` while preserving public response shape.
- [x] 3.4 Add or update tests that verify search filtering, pagination, and file/folder result behavior remain unchanged.

## 4. Repository Primitive Preparation

- [x] 4.1 Identify repeated transaction-aware table operations in upload, file mutation, content reference, folder, and trash flows.
- [x] 4.2 Introduce only the smallest repository primitives needed for repeated upload task or content operations; avoid broad CRUD abstractions.
- [x] 4.3 Keep complex read-model SQL in query objects rather than moving it into generic repositories.
- [x] 4.4 Verify migrated repository primitives preserve existing error handling and transaction-client compatibility.

## 5. Database-only TransactionRunner

- [x] 5.1 Design the coroutine-friendly `TransactionRunner` API and its `Result<T>`/error mapping behavior.
- [x] 5.2 Implement transaction creation, callback execution, rollback-on-error, and rollback-on-exception behavior without filesystem hooks.
- [x] 5.3 Migrate only the upload finalization database transaction section to the transaction runner.
- [x] 5.4 Keep blob promotion, temporary-file cleanup, and database-failure compensation explicit around the transaction-runner call.
- [x] 5.5 Verify upload completion success, duplicate-content completion, failure, and compensation paths remain behavior-compatible.

## 6. Storage Boundary Vocabulary and Follow-up Design

- [x] 6.1 Document the intended distinction between `UploadStagingStorage` session/chunk responsibilities and `BlobStore` content/blob responsibilities.
- [x] 6.2 Identify current `IFileStorage` operations that belong to upload staging versus final blob storage.
- [x] 6.3 Preserve the current local blob layout as a compatibility constraint for future storage split work.
- [x] 6.4 Defer implementation of new storage interfaces until upload lifecycle and content registration boundaries are stable.
