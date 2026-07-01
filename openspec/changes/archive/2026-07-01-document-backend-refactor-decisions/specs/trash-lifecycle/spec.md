## MODIFIED Requirements

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
