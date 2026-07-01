## ADDED Requirements

### Requirement: Content Reference Boundary
The persistence design SHALL define `file_contents` lookup, creation, reference-count increments, reference-count decrements, and zero-reference verification as content-domain operations owned by a dedicated backend boundary.

#### Scenario: Existing content is reused
- **WHEN** upload completion, instant upload, or copy reuses an existing content record
- **THEN** the system SHALL update content reference counts through the content-domain boundary and SHALL preserve the calling flow's transaction semantics

#### Scenario: Content reference count reaches zero
- **WHEN** permanent trash deletion or equivalent cleanup decrements content references to zero
- **THEN** the system SHALL verify the zero-reference state before any final blob deletion is attempted

### Requirement: Storage Accounting Boundary
The persistence design SHALL define `users.storage_used` and `users.storage_reserved` mutations as quota/accounting-domain operations owned by a dedicated backend boundary.

#### Scenario: Upload capacity evaluated
- **WHEN** the system decides whether an upload can be initialized
- **THEN** it SHALL use an atomic quota/accounting operation equivalent to comparing used storage, reserved storage, requested bytes, and quota

#### Scenario: Reserved upload bytes finalized
- **WHEN** a non-instant upload completes successfully
- **THEN** the system SHALL atomically convert the upload's reserved bytes into used bytes as part of the completion database consistency boundary

#### Scenario: Reserved upload bytes released
- **WHEN** upload initialization fails after reservation, or an upload is cancelled or expires
- **THEN** the system SHALL release reserved bytes through the quota/accounting boundary

#### Scenario: Accounting reconciliation requested
- **WHEN** administrators or diagnostics need to compare persisted accounting with active and trash state
- **THEN** the quota/accounting boundary SHALL provide a named reconciliation query or helper without changing runtime accounting automatically by default
