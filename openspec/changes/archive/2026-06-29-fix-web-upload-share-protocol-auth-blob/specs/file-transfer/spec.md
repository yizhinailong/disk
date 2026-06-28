## MODIFIED Requirements

### Requirement: Resumable Chunk Upload
The system SHALL support chunked and resumable upload using upload task state and uploaded chunk tracking, and clients SHALL send chunk upload requests using the backend-defined upload task identifier, chunk index, chunk payload, and authenticated owner context.

#### Scenario: Chunk uploaded successfully
- **WHEN** a client uploads a valid chunk for an active upload task using the backend chunk upload contract
- **THEN** the system SHALL store the chunk and record the uploaded chunk index

#### Scenario: Client resumes upload
- **WHEN** a client initializes or continues an upload with existing uploaded chunks
- **THEN** the system SHALL expose which chunks are already uploaded so the client can skip them

#### Scenario: Client chunk protocol is incompatible
- **WHEN** a client uploads a chunk using missing or incompatible upload task, chunk index, payload, or owner authentication data
- **THEN** the system SHALL reject the chunk request without recording it as uploaded

### Requirement: Upload Completion
The system SHALL complete an upload only after all required chunks are present and the assembled file matches expected integrity checks, and clients SHALL call completion using the backend upload task completion contract.

#### Scenario: All chunks are valid
- **WHEN** a client completes an upload with all valid chunks present using the backend completion contract
- **THEN** the system SHALL assemble the file, create file metadata, update storage accounting, and clean temporary state

#### Scenario: Chunks are missing or invalid
- **WHEN** a client attempts to complete an upload with missing or invalid chunks
- **THEN** the system SHALL reject completion and preserve enough state for retry or cleanup
