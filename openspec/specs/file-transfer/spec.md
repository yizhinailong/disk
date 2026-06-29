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

### Requirement: Download Integrity Metadata
The system SHALL provide clients with download metadata sufficient to verify expected byte size and, when available, content hash or checksum before or during file content transfer.

#### Scenario: Owner download metadata includes integrity fields
- **WHEN** an authenticated owner requests download information for an accessible file
- **THEN** the system SHALL return expected byte size and available content hash or checksum metadata for that file

#### Scenario: Visitor download metadata includes integrity fields
- **WHEN** a visitor requests download information or browse metadata for an accessible shared file using a valid share token
- **THEN** the system SHALL return expected byte size and available content hash or checksum metadata permitted for visitor download verification

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

### Requirement: Visitor Ranged File Download
The system SHALL support HTTP byte range requests for visitor share downloads using share-token authentication consistently with owner ranged downloads.

#### Scenario: Visitor requests valid range
- **WHEN** a visitor requests a valid byte range for an accessible shared file with a valid share token
- **THEN** the system SHALL return partial content for the requested range without requiring owner bearer authentication

#### Scenario: Visitor requests invalid range
- **WHEN** a visitor requests an unsatisfiable byte range for an accessible shared file
- **THEN** the system SHALL reject the request with a range error and SHALL NOT return a misleading successful full-file response

### Requirement: Download Resume Consistency
The system SHALL keep download metadata stable enough for clients to decide whether a partial local file can be resumed safely.

#### Scenario: Client compares resume metadata
- **WHEN** a client obtains metadata for a file before resuming a partial download
- **THEN** the metadata SHALL include stable file identity, expected total size, and available integrity fields needed to detect incompatible partial files
