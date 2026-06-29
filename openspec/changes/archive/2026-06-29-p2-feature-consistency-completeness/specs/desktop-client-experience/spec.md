## ADDED Requirements

### Requirement: Desktop Visitor Resumable Download
The Desktop Visitor Shell SHALL support true breakpoint resume for visitor share downloads by using share-token authenticated byte-range requests and persisted partial download state.

#### Scenario: Visitor resumes interrupted download
- **WHEN** a visitor restarts a download for a shared file with a trusted partial local file
- **THEN** the Desktop client SHALL send the share token with a Range request for the remaining bytes and append the response to the partial file

#### Scenario: Visitor resume is not safe
- **WHEN** the partial file size exceeds expected size, content identity metadata changed, or the server rejects the requested range
- **THEN** the Desktop client SHALL restart or fail the download explicitly instead of silently treating the partial file as resumed

### Requirement: Desktop Download Completion Integrity Check
The Desktop client SHALL verify visitor download completion using expected byte size and available hash or checksum metadata before reporting success.

#### Scenario: Visitor download verification succeeds
- **WHEN** a visitor download completes and the local file matches expected size and available hash or checksum metadata
- **THEN** the Desktop client SHALL mark the transfer complete and make the file available to the user

#### Scenario: Visitor download verification fails
- **WHEN** the completed local file does not match expected size or available hash or checksum metadata
- **THEN** the Desktop client SHALL mark the transfer failed, show clear feedback, and preserve retry or restart behavior without presenting the file as successfully downloaded

#### Scenario: Visitor download has no hash metadata
- **WHEN** visitor download metadata includes expected size but no hash or checksum
- **THEN** the Desktop client SHALL verify size, report completion only after the size matches, and keep the missing hash as a lower-confidence validation state rather than inventing a successful hash check
