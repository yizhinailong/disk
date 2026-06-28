# File Transfer Specification

## Purpose

Defines upload, upload accounting, resumable chunk transfer, instant upload, completion, cancellation, and ranged download behavior.

## Requirements

### Requirement: Upload Initialization
The system SHALL initialize file uploads by validating the target folder, filename, file size, file hash, and user storage availability.

#### Scenario: New upload is initialized
- **WHEN** an authenticated user initializes an upload with valid metadata and sufficient storage
- **THEN** the system SHALL create an upload task and return upload instructions including chunk information

#### Scenario: Upload cannot be initialized
- **WHEN** the upload metadata is invalid, target folder is inaccessible, or storage is insufficient
- **THEN** the system SHALL reject initialization without creating an active upload task

### Requirement: Upload Storage Reservation
The system SHALL reserve user storage during upload initialization and SHALL release or convert that reservation during upload cancellation, expiry, or completion.

#### Scenario: Upload initialized with reservation
- **WHEN** a non-instant upload is initialized
- **THEN** the system SHALL increase reserved storage by the upload size

#### Scenario: Upload completed
- **WHEN** an upload completes successfully
- **THEN** the system SHALL convert reserved storage into used storage atomically

#### Scenario: Upload cancelled or expired
- **WHEN** an upload is cancelled or expires before completion
- **THEN** the system SHALL release the reserved storage

### Requirement: Instant Upload
The system SHALL support instant upload when uploaded content already exists and can be referenced safely.

#### Scenario: Existing content matches upload hash
- **WHEN** upload initialization finds existing content matching the requested file hash
- **THEN** the system SHALL create file metadata referencing the existing content without requiring chunk upload

### Requirement: Resumable Chunk Upload
The system SHALL support chunked and resumable upload using upload task state and uploaded chunk tracking.

#### Scenario: Chunk uploaded successfully
- **WHEN** a client uploads a valid chunk for an active upload task
- **THEN** the system SHALL store the chunk and record the uploaded chunk index

#### Scenario: Client resumes upload
- **WHEN** a client initializes or continues an upload with existing uploaded chunks
- **THEN** the system SHALL expose which chunks are already uploaded so the client can skip them

### Requirement: Upload Completion
The system SHALL complete an upload only after all required chunks are present and the assembled file matches expected integrity checks.

#### Scenario: All chunks are valid
- **WHEN** a client completes an upload with all valid chunks present
- **THEN** the system SHALL assemble the file, create file metadata, update storage accounting, and clean temporary state

#### Scenario: Chunks are missing or invalid
- **WHEN** a client attempts to complete an upload with missing or invalid chunks
- **THEN** the system SHALL reject completion and preserve enough state for retry or cleanup

### Requirement: File Download Metadata
The system SHALL provide download metadata before file content transfer.

#### Scenario: Download info requested
- **WHEN** an authenticated user requests download information for an accessible file
- **THEN** the system SHALL return file metadata needed by the client to download the file

### Requirement: Ranged File Download
The system SHALL support HTTP byte range requests for file downloads.

#### Scenario: Full download
- **WHEN** a client requests a file without a Range header
- **THEN** the system SHALL return the full file content

#### Scenario: Valid range download
- **WHEN** a client requests a valid byte range
- **THEN** the system SHALL return partial content for the requested range

#### Scenario: Invalid range download
- **WHEN** a client requests an unsatisfiable byte range
- **THEN** the system SHALL reject the request with a range error

### Requirement: Resumable Client Download
The system SHALL allow clients to resume interrupted downloads from an existing partial local file when download metadata and the content endpoint support HTTP byte ranges.

#### Scenario: Client resumes from partial file
- **WHEN** a client has a partial local file smaller than the expected file size and download metadata indicates range support
- **THEN** the client SHALL request the remaining bytes using a `Range` header and append the response to the partial file

#### Scenario: Partial file cannot be resumed
- **WHEN** a partial local file is missing, empty, larger than the expected size, or the download endpoint does not support ranges
- **THEN** the client SHALL restart the download as a full transfer instead of appending to the invalid partial file

#### Scenario: Range resume rejected by server
- **WHEN** a client resume request receives an unsatisfiable range response
- **THEN** the client SHALL discard the invalid partial state and retry or restart as a full download according to retry policy

### Requirement: Download Completion Integrity Validation
The system SHALL require clients to validate completed download files against expected metadata before marking a download task completed.

#### Scenario: Completed download size matches expected size
- **WHEN** a download transfer finishes successfully and the local file size equals the expected file size
- **THEN** the client SHALL allow completion to proceed to any required hash validation

#### Scenario: Completed download size does not match expected size
- **WHEN** a download transfer finishes but the local file size differs from the expected file size
- **THEN** the client SHALL mark the download failed with an integrity error and SHALL NOT report the task as completed

#### Scenario: Completed download hash matches expected hash
- **WHEN** a download transfer finishes and expected file hash metadata is available
- **THEN** the client SHALL compute the local file hash and mark the task completed only if it matches the expected hash

#### Scenario: Completed download hash does not match expected hash
- **WHEN** a download transfer finishes and the computed local file hash differs from the expected file hash
- **THEN** the client SHALL mark the download failed with an integrity error and SHALL NOT report the task as completed
