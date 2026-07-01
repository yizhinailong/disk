## Context

The backend refactor TODO identifies infrastructure evolution after safety-net, domain-boundary, and transaction-boundary preparation. Current services such as `FileQueryService`, `UploadService`, `FileMutationService`, and `CleanupService` still combine orchestration with persistence details, raw SQL, cache-key construction, explicit transactions, and storage side effects.

The intended direction is incremental. The codebase already has useful boundaries such as `IFileStorage`, service classes, Drogon coroutine database APIs, and explicit compensation logic in upload and cleanup flows. This change should name and isolate those infrastructure responsibilities without introducing a large framework-style abstraction or changing public API behavior.

## Goals / Non-Goals

**Goals:**

- Extract complex read SQL into named query objects, starting with file-list and search flows.
- Make file-list cache identity match the complete query identity, including pagination size.
- Introduce repository primitives only for stable table-level persistence operations.
- Introduce a small coroutine-friendly transaction runner once query/repository boundaries are established.
- Keep database transactions and filesystem side effects visibly separate.
- Define the vocabulary and direction for splitting upload staging storage from final blob storage later.
- Preserve current API response shapes, route behavior, database schema, and local storage layout during the first implementation slices.

**Non-Goals:**

- Do not introduce a generic CRUD repository framework.
- Do not rewrite every service in one change.
- Do not hide filesystem compensation inside a broad UnitOfWork abstraction.
- Do not migrate from local filesystem storage to S3, MinIO, or another object store.
- Do not change database schema as part of the first query-object slice.
- Do not change product semantics for quota, trash, upload completion, or content reference counts unless separately specified.

## Decisions

### Decision 1: Use query objects for complex read models

Complex read paths such as file listing and search should move to named query objects rather than vague repository methods. A file-list query object can own deterministic ordering, count queries, pagination, SQL construction, and row-to-read-model mapping while leaving business orchestration in `FileQueryService`.

Alternatives considered:

- Keep SQL inside service methods. This avoids new files but keeps service methods responsible for too many concerns and makes query semantics harder to test.
- Move all SQL into table repositories. This hides important query semantics behind generic method names and does not fit union/pagination/search read models well.

### Decision 2: Keep repositories small and primitive-oriented

Repositories should be introduced for stable table-level operations such as upload task lookup/update, content reference increments, file insert/delete primitives, folder lookup, and trash row operations. They should not become a generic abstraction over all database access.

Alternatives considered:

- Build a broad repository layer for all tables immediately. This increases churn and risks creating pass-through methods with little semantic value.
- Avoid repositories entirely. This leaves transaction-aware table operations duplicated across services.

### Decision 3: Treat cache identity as part of query identity

File-list cache keys should include all inputs that can affect returned rows or pagination metadata. In particular, `page_size` should be included alongside user, parent folder, type, sort field, sort order, and page.

Alternatives considered:

- Keep the existing cache key shape. This preserves exact key strings but allows requests with different page sizes to collide.
- Disable file-list caching during refactor. This avoids cache-key mistakes but removes existing performance behavior and broadens the change unnecessarily.

### Decision 4: Introduce a database-only TransactionRunner

The transaction helper should standardize Drogon coroutine transaction creation, rollback behavior, and error mapping for database operations. It should pass a transaction client into a callback and return a `Result<T>` or equivalent project result type.

It should not manage filesystem operations, blob promotion, chunk assembly, or compensation. Those side effects should remain explicit in the service or lifecycle layer.

Alternatives considered:

- Full UnitOfWork with filesystem compensation hooks. This is more powerful but hides failure-domain boundaries and is too heavy for the current refactor stage.
- Continue hand-writing every transaction. This preserves local clarity but keeps repeated begin/rollback/error patterns scattered.

### Decision 5: Use upload finalization as the first TransactionRunner trial

Upload finalization is the best first trial for the transaction runner after query/repository boundaries are in place because it is high-value and already has a visible database transaction section. Blob promotion and compensation should remain explicit around the transaction.

Alternatives considered:

- Start with expired trash cleanup. It has clear transaction chunks, but also includes cursor batching, reference-count decrements, quota release, and post-transaction blob deletion, making it a riskier first abstraction target.
- Start with copy/delete flows. They are useful later, but they overlap more with quota/content/trash domain-boundary extraction.

### Decision 6: Defer storage split until upload/content boundaries are stable

`UploadStagingStorage` and `BlobStore` should be designed around distinct identities: upload staging uses upload sessions and chunk indexes, while final blob storage uses content hashes or blob descriptors. The current local filesystem implementation and `build/uploaded/{md5_prefix}/{md5}.bin` layout should remain compatible.

This split should be implemented after upload lifecycle and content registration responsibilities are stable enough to avoid guessing interface ownership.

Alternatives considered:

- Split `IFileStorage` immediately. This risks designing interfaces around the current monolithic `UploadService` responsibilities.
- Leave storage as a single abstraction indefinitely. This keeps current behavior simple but makes object-storage compatibility and failure-domain clarity harder later.

## Risks / Trade-offs

- Query objects may become thin wrappers around SQL without improving clarity → Require names to reflect read-model semantics and keep complex SQL visible.
- Repository primitives may grow into a vague CRUD layer → Limit repositories to repeated, stable table operations and prefer query objects for complex reads.
- Cache-key normalization changes Redis key shape → Treat old entries as naturally expiring and keep TTL behavior unchanged unless separately changed.
- TransactionRunner may hide important failure behavior → Keep it database-only and require filesystem compensation to stay explicit at call sites.
- Upload finalization refactor could affect a critical flow → Apply TransactionRunner to one DB section first and rely on existing/new upload invariant tests.
- Storage split may overfit local filesystem paths → Use staging-object and blob-descriptor vocabulary in design, even if the first implementation wraps current paths.

## Migration Plan

1. Extract `FileListQuery` and preserve existing file-list response behavior.
2. Normalize file-list cache identity to include all query-affecting inputs, including `page_size`.
3. Extract `SearchQuery` after the file-list pattern is stable.
4. Add repository primitives where repeated transaction-aware table operations become clear.
5. Introduce a small database-only `TransactionRunner` and apply it first to upload finalization.
6. Document `UploadStagingStorage` and `BlobStore` vocabulary before implementation.
7. Implement the storage split later by wrapping the current local filesystem behavior without changing stored blob layout.

Rollback is straightforward for early slices: revert the query-object or transaction-runner extraction while preserving unchanged SQL and service behavior. Redis cache-key changes can roll forward or back because old keys are short-lived cache entries.

## Open Questions

- Should query objects return DTO/read-model types directly, or lower-level rows that services map to DTOs?
- Should `FileListCachePolicy` live beside query objects, Redis service helpers, or file services?
- What exact `Result<T>` shape should the first `TransactionRunner` expose for coroutine callbacks?
- When storage split begins, should blob identity be represented primarily by content hash, content id, or a dedicated descriptor type?
