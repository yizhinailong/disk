## ADDED Requirements

### Requirement: Client Resumable Download Integration
Clients SHALL use backend download metadata and HTTP byte-range behavior to resume interrupted downloads when the local partial file can be matched to the same remote content.

#### Scenario: Client resumes partial download
- **WHEN** a client has a partial local file and metadata confirms it belongs to the same accessible remote file
- **THEN** the client SHALL request the remaining bytes with a valid Range header and append the response to the partial file

#### Scenario: Partial file cannot be trusted
- **WHEN** local partial size, file identity, expected total size, or available integrity metadata does not match the remote download metadata
- **THEN** the client SHALL discard or restart the partial download instead of appending incompatible bytes

### Requirement: Client Download Completion Verification
Clients SHALL verify completed downloads against expected size and available hash or checksum metadata before presenting the transfer as successful.

#### Scenario: Download matches expected metadata
- **WHEN** a completed download's byte size and available hash or checksum match the expected download metadata
- **THEN** the client SHALL mark the download as successful

#### Scenario: Download fails verification
- **WHEN** a completed download's byte size or available hash or checksum does not match expected metadata
- **THEN** the client SHALL mark the download as failed, preserve enough state for retry or restart, and SHALL NOT present the corrupted file as successfully downloaded

### Requirement: Client Memory-Safe Download Handling
Clients SHALL avoid unnecessary full-payload buffering for large downloads and SHALL keep transfer payload bytes out of shared UI state stores.

#### Scenario: Large download handled by client
- **WHEN** a client downloads a large file
- **THEN** the client SHALL stream, write incrementally, or delegate saving to platform download primitives rather than storing the complete payload in shared UI state
