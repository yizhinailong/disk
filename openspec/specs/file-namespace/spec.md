# File Namespace Specification

## Purpose

Defines user-visible file and folder namespace behavior, including listing, metadata, navigation, search, and mutation operations.

## Requirements

### Requirement: File And Folder Listing
The system SHALL list files and folders within a user's directory namespace and distinguish item types in the response.

#### Scenario: Directory listing succeeds
- **WHEN** an authenticated user lists an accessible directory
- **THEN** the system SHALL return files and folders for that directory with pagination and type-specific fields

### Requirement: File And Folder Details
The system SHALL provide metadata details for accessible files and folders.

#### Scenario: Detail requested for accessible item
- **WHEN** an authenticated user requests details for an accessible item
- **THEN** the system SHALL return the item's metadata and location information

### Requirement: Folder Creation
The system SHALL allow users to create folders under valid parent folders while enforcing folder naming rules.

#### Scenario: Folder creation succeeds
- **WHEN** an authenticated user creates a folder with a valid name under an accessible parent
- **THEN** the system SHALL atomically create the folder, increment the parent item count when applicable, and return its metadata

#### Scenario: Folder name is invalid or conflicts
- **WHEN** the folder name is invalid or conflicts with an existing folder in the same parent
- **THEN** the system SHALL reject the creation request

#### Scenario: Parent count update fails during nested creation
- **WHEN** PostgreSQL rejects the parent folder item-count update for a nested folder creation
- **THEN** the transaction SHALL leave both the child folder inventory and parent item count unchanged

#### Scenario: The same folder name is created concurrently
- **WHEN** multiple requests concurrently create the same name for one user and parent folder
- **THEN** exactly one request SHALL create the folder and increment the parent count, while every loser SHALL receive `FolderAlreadyExists`

### Requirement: Rename Items
The system SHALL allow users to rename files or folders while enforcing naming validation and conflict rules.

#### Scenario: Rename succeeds
- **WHEN** an authenticated user renames an accessible file or folder to a valid non-conflicting name
- **THEN** the system SHALL update the item name and modification metadata

#### Scenario: Rename target conflicts
- **WHEN** the requested new name violates naming rules or conflicts with an existing same-type item
- **THEN** the system SHALL reject the rename request

#### Scenario: Different files are concurrently renamed to one name
- **WHEN** multiple requests concurrently rename different files in one folder to the same valid name
- **THEN** exactly one transaction SHALL update its file name and path, while every loser SHALL receive `FileAlreadyExists` without a false not-found result

#### Scenario: Different folders are concurrently renamed to one name
- **WHEN** multiple requests concurrently rename different folders in one parent to the same valid name
- **THEN** exactly one transaction SHALL update its complete folder/file subtree, while every loser SHALL receive `FolderAlreadyExists` without partial path changes

### Requirement: Move Items
The system SHALL allow users to move files or folders to an accessible target folder.

#### Scenario: Move succeeds
- **WHEN** an authenticated user moves accessible items to a valid target folder
- **THEN** the system SHALL update the items' parent location and return the number of moved items

#### Scenario: Move target is invalid
- **WHEN** the target folder is missing, inaccessible, or would create an invalid folder relationship
- **THEN** the system SHALL reject the invalid move

#### Scenario: Same-named files are concurrently moved into one folder
- **WHEN** multiple requests concurrently move same-named files from different source folders into one target folder
- **THEN** exactly one file SHALL move, while every other conflicting item SHALL remain at its source and its batch request SHALL succeed with that item excluded from the moved count

### Requirement: Copy Items
The system SHALL allow users to copy files or folders while preserving content-sharing and quota semantics.

#### Scenario: Copy succeeds
- **WHEN** an authenticated user copies accessible items with sufficient quota
- **THEN** the system SHALL create copied metadata and update content reference counts as needed

#### Scenario: Copy exceeds quota
- **WHEN** the requested copy would exceed user quota
- **THEN** the system SHALL reject the copy without partially applying the operation

#### Scenario: Same-named files are concurrently copied into one folder
- **WHEN** multiple requests concurrently copy the same-named file into one target folder
- **THEN** exactly one copy SHALL be created, while every other conflicting item SHALL be excluded from the copied count without consuming content references, used quota, or reserved quota

### Requirement: Search Namespace
The system SHALL allow users to search files and folders by keyword within their own namespace.

#### Scenario: Search succeeds
- **WHEN** an authenticated user searches with a valid keyword and optional scope
- **THEN** the system SHALL return matching accessible files and folders with pagination

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
