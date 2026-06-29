## ADDED Requirements

### Requirement: Web Administrator Storage Editing
The Web client SHALL allow authenticated administrators to modify supported user storage quota values from the user administration experience while preserving backend authorization and validation as the source of truth.

#### Scenario: Administrator opens storage edit action
- **WHEN** an authenticated administrator views a target user's details or row actions in Web user administration
- **THEN** the Web client SHALL expose a storage quota edit action showing current used storage and current quota

#### Scenario: Storage quota update succeeds
- **WHEN** an administrator submits a valid storage quota update for another user
- **THEN** the Web client SHALL call the administrator storage update API, show success feedback, and refresh the affected user detail/list data

#### Scenario: Storage quota update is rejected
- **WHEN** the backend rejects a storage quota update because of authorization, invalid bounds, target user state, or quota consistency rules
- **THEN** the Web client SHALL show the backend error and SHALL NOT present the stale edited value as saved

### Requirement: Web Folder Tree Store Integration
The Web client SHALL use the centralized store as the authoritative state for folder tree data, current folder, selected folder, breadcrumb/list refresh, and tree navigation side effects.

#### Scenario: Folder selected from tree
- **WHEN** a user selects a folder in the Web folder tree
- **THEN** the Web client SHALL update the centralized current-folder state and refresh the file list and breadcrumb for that folder

#### Scenario: Folder changes outside tree
- **WHEN** a user changes folders through breadcrumb, file-list drill-down, up navigation, creation, rename, move, or deletion
- **THEN** the Web client SHALL update or invalidate centralized folder tree state so tree selection and visible hierarchy remain consistent with the active folder context

#### Scenario: Folder tree refresh fails
- **WHEN** refreshing folder tree data fails
- **THEN** the Web client SHALL preserve the last safe navigation state where possible and surface a recoverable error without corrupting current folder or list state

### Requirement: Web Memory-Safe Large File Downloads
The Web client SHALL download large files without storing complete file payloads in centralized reactive state or otherwise requiring full application-memory buffering before save.

#### Scenario: Large file download starts
- **WHEN** a user starts downloading a large accessible file from the Web client
- **THEN** the Web client SHALL stream, pipe, or use browser download primitives so the payload is not retained in application store state

#### Scenario: Download progress and errors are shown
- **WHEN** a Web download is in progress, interrupted, rejected, or completed
- **THEN** the Web client SHALL show accurate progress or terminal feedback without exposing partial content as a successful completed download

#### Scenario: Browser cannot use preferred streaming path
- **WHEN** the browser environment does not support the preferred memory-safe download path
- **THEN** the Web client SHALL use the safest supported fallback and SHALL keep large payloads out of centralized application state
