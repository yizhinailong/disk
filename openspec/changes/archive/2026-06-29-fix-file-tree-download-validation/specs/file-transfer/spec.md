## ADDED Requirements

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
