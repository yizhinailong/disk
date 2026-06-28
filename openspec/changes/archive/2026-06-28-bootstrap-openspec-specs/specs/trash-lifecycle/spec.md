## ADDED Requirements

### Requirement: Soft Delete
The system SHALL move deleted files or folders into trash instead of immediately deleting recoverable user content.

#### Scenario: Item is deleted
- **WHEN** an authenticated user deletes an accessible file or folder
- **THEN** the system SHALL remove it from its active namespace and create trash state for possible recovery

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
