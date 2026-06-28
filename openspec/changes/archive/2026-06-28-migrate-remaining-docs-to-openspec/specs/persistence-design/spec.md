## ADDED Requirements

### Requirement: PostgreSQL persistence baseline
The persistence design SHALL document PostgreSQL-specific relational persistence constraints for users, files, folders, file contents, upload tasks, trash, shares, share associations, and operation logs, and Redis-specific constraints for cache, session, token, and rate-limit state. The architecture-decision capability SHALL remain the canonical source for the database selection decision.

#### Scenario: New deployment persistence selection
- **WHEN** a new environment is provisioned from the documented persistence design
- **THEN** the persistence documentation SHALL describe PostgreSQL database constraints and Redis cache/session/ephemeral-state constraints without duplicating the architecture-decision rationale

### Requirement: Relational schema contracts
The persistence design SHALL define table-level responsibilities, primary keys, important foreign keys, uniqueness constraints, soft-delete behavior, audit timestamps, naming conventions, and indexing expectations for the core schema.

#### Scenario: Schema names reviewed
- **WHEN** database objects are documented or added
- **THEN** table names and column names SHALL use lower-case snake_case, primary keys SHALL use `id`, foreign keys SHALL use `<entity>_id`, regular indexes SHALL use `idx_<table>_<columns>`, and unique indexes SHALL use `uk_<table>_<columns>` naming patterns

#### Scenario: User and role data persisted
- **WHEN** a user account is stored
- **THEN** the schema SHALL retain unique username/email identity, password hash, quota fields, account status, role, login tracking, and audit timestamps

#### Scenario: File metadata persisted
- **WHEN** file metadata is stored
- **THEN** the schema SHALL connect each logical file to its owner, folder context, deduplicated file content record, path/name metadata, size/MIME metadata, counters, and audit timestamps

#### Scenario: Folder hierarchy persisted
- **WHEN** a folder is stored
- **THEN** the schema SHALL retain owner, parent folder, path, depth, item-count, and uniqueness rules needed for directory-tree and breadcrumb queries

### Requirement: Upload task lifecycle persistence
The persistence design SHALL model chunked upload tasks and uploaded chunk records so interrupted uploads, reserved storage, expiry, cancellation, and completion can be tracked safely.

#### Scenario: Upload task is created
- **WHEN** an upload is initialized
- **THEN** the database SHALL persist task identity, owner, target folder, filename, file size/hash, chunk size/count, reserved bytes, temporary path, status, expiry, and audit fields

#### Scenario: Chunk is recorded concurrently
- **WHEN** a chunk upload completes
- **THEN** the uploaded chunk record SHALL be keyed by task and chunk index so duplicate concurrent chunk writes do not create duplicate progress records

#### Scenario: Upload task ends
- **WHEN** an upload completes, is cancelled, or expires
- **THEN** reserved storage semantics and task finalization fields SHALL allow cleanup and quota reconciliation

### Requirement: Storage accounting and reference counts
The persistence design SHALL define storage accounting and deduplicated content reference-count semantics across active files and trash records.

#### Scenario: Effective quota calculated
- **WHEN** upload capacity is evaluated
- **THEN** effective available quota SHALL be computed from storage quota minus used storage and reserved upload bytes

#### Scenario: File moves to trash
- **WHEN** a file is soft-deleted into trash
- **THEN** the file content reference SHALL remain counted until the trash record is permanently deleted

#### Scenario: Trash file is permanently deleted
- **WHEN** a trash file is permanently deleted
- **THEN** the associated file content reference count SHALL be decremented and blob cleanup SHALL be possible when the count reaches zero

### Requirement: Search and query indexing
The persistence design SHALL specify indexes for high-frequency lookup, list, cleanup, sharing, operation-log, JSONB, and file/folder-name search paths.

#### Scenario: Folder file list query
- **WHEN** the system lists a user's files in a folder
- **THEN** the schema SHALL provide indexes that support filtering by user and folder efficiently

#### Scenario: Expired cleanup query
- **WHEN** cleanup jobs find expired upload tasks, trash entries, or shares
- **THEN** the schema SHALL provide indexes suitable for expiration-time scanning

#### Scenario: JSONB or text search query
- **WHEN** operation-log details, file names, or folder names are searched with PostgreSQL features
- **THEN** the schema SHALL allow GIN indexing for `operation_logs.details`, `files.name`, and `folders.name` where those search paths are enabled

### Requirement: PostgreSQL persistence constraints
The persistence design SHALL align with the canonical PostgreSQL database-selection decision recorded by the architecture-decision capability while documenting only the persistence constraints needed by schema, ORM, database-change, and backup design.

#### Scenario: Database choice reviewed from persistence design
- **WHEN** a reviewer asks why PostgreSQL-specific schema or migration constraints appear in persistence documentation
- **THEN** the persistence design SHALL point to the architecture-decision capability as the canonical source for the database-selection rationale

#### Scenario: Persistence rollback requirements reviewed
- **WHEN** database rollback or restore behavior is needed for persistence operations
- **THEN** the persistence design SHALL describe the schema, data synchronization, backup, and restore constraints without duplicating the architecture-decision rationale
