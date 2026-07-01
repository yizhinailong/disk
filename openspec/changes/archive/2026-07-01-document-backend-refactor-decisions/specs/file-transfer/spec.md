## MODIFIED Requirements

### Requirement: Instant Upload
The system SHALL support instant upload when uploaded content already exists and can be referenced safely, and instant upload SHALL apply logical per-user storage accounting consistently with other user-visible file creation paths.

#### Scenario: Existing content matches upload hash
- **WHEN** upload initialization finds existing content matching the requested file hash
- **THEN** the system SHALL create file metadata referencing the existing content without requiring chunk upload

#### Scenario: Instant upload consumes logical quota
- **WHEN** instant upload creates a new user-visible file reference to existing content
- **THEN** the system SHALL increase that user's used storage by the logical file size and SHALL enforce quota using logical per-user bytes

### Requirement: Upload Lifecycle Boundary
The backend SHALL expose upload lifecycle behavior through an explicit domain boundary that preserves upload initialization, chunk acceptance, completion, cancellation, expiry, quota, content, temporary-storage, and response semantics while applying accepted accounting decisions for logical per-user storage.

#### Scenario: Non-instant upload initialized
- **WHEN** an authenticated user initializes a valid non-instant upload with sufficient quota
- **THEN** the upload lifecycle SHALL reserve storage, create an in-progress upload task, return chunk instructions, and avoid creating file metadata until completion

#### Scenario: Instant upload initialized
- **WHEN** upload initialization finds existing content matching the requested file hash
- **THEN** the upload lifecycle SHALL create file metadata referencing the existing content, update content reference state safely, and increase used storage according to logical per-user accounting semantics

#### Scenario: Upload completion succeeds
- **WHEN** an upload completes with all chunks present and matching integrity metadata
- **THEN** the upload lifecycle SHALL assemble and verify content, reuse or create content metadata, create file metadata, convert reserved storage to used storage atomically with database finalization, mark the task completed, clean chunk tracking, and clean temporary artifacts

#### Scenario: Upload completion fails after blob promotion
- **WHEN** final blob promotion succeeds but later database finalization fails
- **THEN** the upload lifecycle SHALL attempt explicit compensation for the promoted blob and SHALL report/log any orphan-risk condition

#### Scenario: Upload cancelled or expired
- **WHEN** an in-progress upload is cancelled by the user or expires by scheduled cleanup
- **THEN** the upload lifecycle SHALL release reserved storage, move the task to the appropriate terminal state, clean chunk tracking when applicable, and clean temporary artifacts

## ADDED Requirements

### Requirement: File Download Metadata Updates
The system SHALL update file-level download metadata for successful content downloads that access a file through private owner APIs or visitor share APIs.

#### Scenario: Private file content download succeeds
- **WHEN** an authenticated owner successfully downloads file content through the private download endpoint
- **THEN** the system SHALL increment file-level download count and update the file's last-accessed timestamp

#### Scenario: Private download metadata is requested
- **WHEN** an authenticated owner requests private download information without downloading content
- **THEN** the system SHALL NOT count that metadata lookup as a file content download

#### Scenario: Shared file content download succeeds
- **WHEN** a visitor successfully downloads shared file content using a valid share token
- **THEN** the system SHALL increment share-level download count and SHALL also update file-level download count and last-accessed timestamp

#### Scenario: Share download metadata is requested
- **WHEN** a visitor requests shared download information without downloading content
- **THEN** the system SHALL NOT count that metadata lookup as a file content download or share content download
