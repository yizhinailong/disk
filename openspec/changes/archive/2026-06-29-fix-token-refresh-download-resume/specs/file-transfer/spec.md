## MODIFIED Requirements

### Requirement: Ranged File Download
The system SHALL support HTTP byte range requests for file downloads, and clients that resume downloads SHALL request byte ranges that match already received content before appending resumed bytes.

#### Scenario: Full download
- **WHEN** a client requests a file without a Range header
- **THEN** the system SHALL return the full file content

#### Scenario: Valid range download
- **WHEN** a client requests a valid byte range
- **THEN** the system SHALL return partial content for the requested range

#### Scenario: Invalid range download
- **WHEN** a client requests an unsatisfiable byte range
- **THEN** the system SHALL reject the request with a range error

#### Scenario: Client resumes from known offset
- **WHEN** a client has already received a prefix of a downloadable file and the file supports range requests
- **THEN** the client SHALL request a byte range beginning at the first missing byte before appending resumed content to the existing bytes

#### Scenario: Resume response cannot be matched
- **WHEN** a client attempts to resume a ranged download and the response does not confirm the requested starting offset
- **THEN** the client SHALL NOT append the response to existing partial bytes
