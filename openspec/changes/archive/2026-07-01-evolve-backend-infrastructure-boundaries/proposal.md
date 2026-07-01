## Why

Backend service code currently mixes business orchestration with complex SQL construction, cache-key identity, ad-hoc transaction handling, and storage side-effect coordination. The next backend refactor phase needs explicit infrastructure boundaries so query semantics, database transactions, and file-storage failure domains can evolve incrementally without changing public API behavior.

## What Changes

- Introduce named query objects for complex read paths, starting with file-list and search queries.
- Define file-list cache identity as part of the query contract, including pagination inputs such as `page_size`.
- Add small repository primitives only where they clarify table-level persistence operations; avoid a broad generic CRUD repository layer.
- Introduce a lightweight database-only transaction runner for coroutine flows after query/repository boundaries are established.
- Keep filesystem and database compensation behavior explicit instead of hiding it inside a large UnitOfWork abstraction.
- Define a later storage-boundary direction that separates upload staging storage from final blob storage while preserving the current local filesystem layout.
- Preserve existing API response envelopes and product behavior unless a task explicitly documents a narrow behavior fix such as cache identity correction.

## Capabilities

### New Capabilities
- `backend-infrastructure-boundaries`: Internal backend architecture contracts for query objects, repository primitives, transaction handling, and storage-boundary evolution.

### Modified Capabilities

## Impact

- Affected code areas include `FileQueryService`, `UploadService`, `FileMutationService`, `CleanupService`, storage abstractions, and related tests.
- Public API routes and response shapes are not intended to change.
- Database schema changes are not required for the first slice.
- Redis file-list cache keys may change to include complete query identity.
- Storage backend behavior remains local-filesystem compatible; S3/MinIO support is design-only for this change unless proposed separately later.
