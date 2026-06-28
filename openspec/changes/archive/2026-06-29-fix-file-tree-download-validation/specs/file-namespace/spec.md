## MODIFIED Requirements

### Requirement: Folder Navigation Metadata
The system SHALL provide folder tree and breadcrumb information for client navigation, and clients SHALL be able to refresh that navigation metadata so visible folder hierarchy remains consistent after folder-affecting operations.

#### Scenario: Folder tree requested
- **WHEN** an authenticated user requests the folder tree
- **THEN** the system SHALL return accessible folder hierarchy data

#### Scenario: Breadcrumb requested
- **WHEN** an authenticated user requests a breadcrumb for an accessible folder
- **THEN** the system SHALL return the path from root to that folder

#### Scenario: Folder tree refreshed after hierarchy mutation
- **WHEN** a client completes a folder-affecting operation such as create, rename, move, delete, trash, or restore
- **THEN** the client SHALL refresh or invalidate folder tree navigation metadata before presenting the affected hierarchy as current

#### Scenario: Current folder remains highlighted after tree refresh
- **WHEN** folder tree navigation metadata is refreshed while the user is viewing a folder
- **THEN** the client SHALL keep the active folder context and current-folder highlight synchronized with the refreshed hierarchy
