# Trash Lifecycle Specification

## Purpose

Defines soft deletion, trash listing, restore, permanent deletion, empty trash, expiry cleanup, and storage-release behavior.

## Requirements

### Requirement: Soft Delete
The system SHALL move deleted files or folders into trash instead of immediately deleting recoverable user content.

#### Scenario: Item is deleted
- **WHEN** an authenticated user deletes an accessible file or folder
- **THEN** the system SHALL remove it from its active namespace and create trash state for possible recovery

#### Scenario: A folder subtree changes during soft delete
- **WHEN** files or folders are concurrently copied, uploaded, created, or moved into a folder subtree being soft-deleted
- **THEN** the delete transaction SHALL stabilize the subtree under row locks so committed descendants enter the trash snapshot and no active row references a removed folder

#### Scenario: Soft-deleted item still consumes storage
- **WHEN** an item is moved to trash
- **THEN** the system SHALL continue counting its storage usage until permanent deletion

### Requirement: Trash Listing
The system SHALL allow users to list their trash items with deletion and expiry metadata.

#### Scenario: Trash contains items
- **WHEN** an authenticated user lists trash
- **THEN** the system SHALL return the user's trash items with pagination and original-location metadata

#### Scenario: Trash is empty
- **WHEN** an authenticated user lists an empty trash
- **THEN** the system SHALL return an empty item list and zero total count

### Requirement: Restore Trash Items
The system SHALL allow users to restore trash items when possible and SHALL resolve naming conflicts deterministically.

#### Scenario: Original location exists
- **WHEN** a user restores a trash item whose original parent still exists
- **THEN** the system SHALL restore the item to its original location

#### Scenario: Original location is unavailable
- **WHEN** a user restores a trash item whose original parent no longer exists
- **THEN** the system SHALL restore the item to the user's root namespace

#### Scenario: Restore name conflicts
- **WHEN** a restored item name conflicts at the target location
- **THEN** the system SHALL generate a non-conflicting restored name

#### Scenario: File restore consumption fails
- **WHEN** an active file is recreated but its trash row cannot be consumed in the same restore attempt
- **THEN** the restore transaction SHALL roll back every write so a retry consumes the trash item exactly once without changing content references or quota

### Requirement: Permanent Delete
The system SHALL permanently delete trash items and release storage only during permanent deletion or equivalent cleanup.

#### Scenario: Trash item permanently deleted
- **WHEN** a user permanently deletes a trash item
- **THEN** the system SHALL remove the trash state, update content reference counts, and release storage when applicable

### Requirement: Empty Trash
The system SHALL allow users to permanently delete all their trash items in a single operation.

#### Scenario: Empty trash requested
- **WHEN** an authenticated user empties trash
- **THEN** the system SHALL permanently delete all trash items owned by that user and report deleted count and released storage

### Requirement: Expiry Cleanup
The system SHALL support cleanup of expired trash items according to retention policy.

#### Scenario: Trash item expires
- **WHEN** a trash item's retention period has elapsed
- **THEN** cleanup SHALL permanently delete the expired item according to permanent-delete semantics

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
Trash lifecycle behavior SHALL preserve the rule that soft-deleted items continue to consume logical per-user storage until permanent deletion or equivalent expiry cleanup.

#### Scenario: Item soft-deleted to trash
- **WHEN** an active file or folder is moved into trash
- **THEN** the system SHALL NOT release used storage merely because the item became recoverable trash

#### Scenario: Trash state permanently removed
- **WHEN** trash state is permanently deleted and is no longer recoverable
- **THEN** the system SHALL release used storage according to the deleted trash item size and content-reference semantics

#### Scenario: Quota is checked while recoverable trash exists
- **WHEN** a user has recoverable trash items and attempts to create or copy additional files
- **THEN** quota checks SHALL include the logical size of those recoverable trash items until they are permanently deleted or expired
