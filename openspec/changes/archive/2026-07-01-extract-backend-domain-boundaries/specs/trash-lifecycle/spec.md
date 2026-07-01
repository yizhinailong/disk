## ADDED Requirements

### Requirement: Trash Lifecycle Boundary
The backend SHALL centralize trash lifecycle semantics so soft delete, restore, manual permanent delete, empty trash, and expired trash cleanup share consistent content, quota, and blob-safety behavior.

#### Scenario: Item is moved to trash through namespace deletion
- **WHEN** a file or folder is soft-deleted from the active namespace
- **THEN** trash state creation SHALL be coordinated by the trash lifecycle boundary while preserving active namespace removal, folder snapshot data, affected share cleanup, and cache invalidation

#### Scenario: Trash item permanently deleted manually
- **WHEN** a user permanently deletes one or more trash items
- **THEN** the trash lifecycle SHALL remove trash state, decrement content references for contained files, release used storage according to existing permanent-delete semantics, and only delete physical blobs after zero-reference verification

#### Scenario: Expired trash cleanup runs
- **WHEN** scheduled cleanup processes expired trash rows
- **THEN** it SHALL invoke the same permanent-delete semantics used by manual permanent deletion, preserving batch limits, per-chunk failure isolation, and diagnostic logging

### Requirement: Trash Accounting Preservation
Trash lifecycle behavior SHALL preserve the rule that soft-deleted items continue to consume storage until permanent deletion or equivalent expiry cleanup.

#### Scenario: Item soft-deleted to trash
- **WHEN** an active file or folder is moved into trash
- **THEN** the system SHALL NOT release used storage merely because the item became recoverable trash

#### Scenario: Trash state permanently removed
- **WHEN** trash state is permanently deleted and is no longer recoverable
- **THEN** the system SHALL release used storage according to the deleted trash item size and content-reference semantics
