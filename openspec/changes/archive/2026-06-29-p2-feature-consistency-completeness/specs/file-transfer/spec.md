## ADDED Requirements

### Requirement: Download Integrity Metadata
The system SHALL provide clients with download metadata sufficient to verify expected byte size and, when available, content hash or checksum before or during file content transfer.

#### Scenario: Owner download metadata includes integrity fields
- **WHEN** an authenticated owner requests download information for an accessible file
- **THEN** the system SHALL return expected byte size and available content hash or checksum metadata for that file

#### Scenario: Visitor download metadata includes integrity fields
- **WHEN** a visitor requests download information or browse metadata for an accessible shared file using a valid share token
- **THEN** the system SHALL return expected byte size and available content hash or checksum metadata permitted for visitor download verification

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
