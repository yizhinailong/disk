## MODIFIED Requirements

### Requirement: Binary Download Contract
The system SHALL support successful binary download responses outside the JSON envelope while preserving JSON error responses for download failures, and clients SHALL bypass JSON envelope unwrapping for successful binary responses.

#### Scenario: Successful binary download
- **WHEN** a client downloads file content successfully
- **THEN** the response SHALL return binary content with appropriate download headers rather than the JSON envelope

#### Scenario: Client receives successful binary response
- **WHEN** a client receives a successful download response configured for binary or Blob content
- **THEN** the client SHALL preserve the binary body and SHALL NOT require `code`, `message`, or `data` envelope fields

#### Scenario: Download error
- **WHEN** a download request fails before streaming content
- **THEN** the response SHALL use the standard JSON error envelope
