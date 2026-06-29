## ADDED Requirements

### Requirement: Feature Consistency Validation
The validation documentation SHALL include coverage for P2 feature consistency behavior across Web admin storage editing, Web folder tree store integration, Desktop visitor resumed downloads, download integrity verification, and Web large-file memory pressure.

#### Scenario: Admin storage edit validation executed
- **WHEN** validation covers Web administrator user management
- **THEN** it SHALL verify successful quota update, invalid quota rejection, non-administrator rejection, and UI refresh/error feedback

#### Scenario: Folder tree state validation executed
- **WHEN** validation covers Web file navigation
- **THEN** it SHALL verify that folder tree selection, current folder, file list, breadcrumb, and refresh behavior remain consistent after tree selection, breadcrumb navigation, file-list drill-down, and folder mutation

#### Scenario: Desktop visitor resume validation executed
- **WHEN** validation covers Desktop visitor downloads under interruption and recovery conditions
- **THEN** it SHALL verify that the client resumes with Range requests when safe and restarts or fails explicitly when partial state cannot be trusted

#### Scenario: Download integrity validation executed
- **WHEN** validation covers completed downloads
- **THEN** it SHALL verify expected-size checks, available hash or checksum checks, mismatch failure handling, and retry or restart behavior

#### Scenario: Web large-file pressure validation executed
- **WHEN** validation covers Web large-file downloads
- **THEN** it SHALL record file size, browser/runtime context, memory behavior or proxy indicators, completion result, and error handling evidence showing that large payloads are not stored in centralized UI state
