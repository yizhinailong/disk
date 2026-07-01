## ADDED Requirements

### Requirement: Named query objects for complex read paths
The backend SHALL represent complex read-model database access with named query objects rather than embedding high-complexity SQL directly in orchestration services or hiding it behind vague repository methods.

#### Scenario: File list query is reviewed
- **WHEN** a developer reviews the file-list read path
- **THEN** the deterministic sorting, count queries, pagination logic, SQL semantics, and row mapping SHALL be discoverable through a named file-list query object

#### Scenario: Search query is reviewed
- **WHEN** a developer reviews the file/folder search read path
- **THEN** the search SQL, filtering, pagination, and read-model mapping SHALL be discoverable through a named search query object

### Requirement: Complete file-list cache identity
The backend SHALL build file-list cache keys from all request inputs that affect the returned rows or pagination metadata.

#### Scenario: Page size differs
- **WHEN** two file-list requests differ by `page_size` while sharing the same user, parent folder, type, sort field, sort order, and page number
- **THEN** the backend SHALL use distinct cache identities for those requests

#### Scenario: Query object result is cached
- **WHEN** a file-list response is cached
- **THEN** the cache key SHALL correspond to the complete normalized file-list query identity used to produce that response

### Requirement: Repository primitives remain explicit
The backend SHALL use repository primitives only for stable table-level persistence operations and SHALL keep complex read-model queries in named query objects.

#### Scenario: Table operation is reused in transaction flow
- **WHEN** upload task, file, folder, content, or trash table operations are reused across services or transaction flows
- **THEN** the backend MAY expose them as explicit repository primitives with names that describe the persistence operation

#### Scenario: Complex read model is extracted
- **WHEN** a read path combines filtering, sorting, pagination, joins, unions, or row mapping into a higher-level response model
- **THEN** the backend SHALL prefer a named query object over a vague repository method

### Requirement: Database-only transaction runner
The backend SHALL provide a lightweight transaction runner for coroutine database flows that standardizes transaction creation, rollback behavior, and result/error mapping without managing filesystem side effects.

#### Scenario: Transaction callback fails
- **WHEN** a transaction-runner callback fails or throws before successful completion
- **THEN** the runner SHALL roll back the database transaction and return an error according to the backend result convention

#### Scenario: Filesystem compensation is needed
- **WHEN** a flow performs filesystem or blob-storage side effects before or after a database transaction
- **THEN** compensation for those side effects SHALL remain explicit in the calling service or lifecycle layer rather than hidden inside the transaction runner

### Requirement: Upload finalization transaction trial
The backend SHALL use upload finalization as the first high-value trial for the transaction runner after query and repository boundaries are established.

#### Scenario: Upload finalization is refactored
- **WHEN** the upload finalization database section is migrated to the transaction runner
- **THEN** blob promotion, temporary-file cleanup, and failed-database compensation SHALL remain visible outside the transaction-runner abstraction

### Requirement: Storage boundary evolution vocabulary
The backend SHALL distinguish upload staging storage responsibilities from final blob storage responsibilities before implementing object-storage-compatible backends.

#### Scenario: Upload staging storage is designed
- **WHEN** upload staging storage is specified or implemented
- **THEN** it SHALL use upload-session and chunk-oriented vocabulary for temporary upload data

#### Scenario: Blob store is designed
- **WHEN** final blob storage is specified or implemented
- **THEN** it SHALL use content/blob-oriented vocabulary and preserve compatibility with the current local blob layout unless a separate migration changes it
