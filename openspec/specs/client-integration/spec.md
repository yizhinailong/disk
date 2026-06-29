# Client Integration Specification

## Purpose

Defines cross-client expectations for desktop, terminal, web, and other clients consuming the Disk REST API.

## Requirements

### Requirement: REST Client Compatibility
The system SHALL expose stable REST API behavior that can be consumed by desktop, terminal, web, and other HTTP clients.

#### Scenario: Client calls documented API
- **WHEN** a supported client calls a documented backend API with valid inputs and authentication
- **THEN** the backend SHALL respond according to the shared API contract

### Requirement: Owner Authentication Domain
Clients SHALL use bearer access tokens for owner/user operations and SHALL keep owner credentials separate from visitor share tokens.

#### Scenario: Owner operation requested
- **WHEN** a client performs a user-owned operation such as file, folder, profile, trash, share management, or admin access
- **THEN** the client SHALL send the owner access token using the bearer authorization header

### Requirement: Visitor Share Domain
Clients SHALL use share tokens for visitor share browse and download flows.

#### Scenario: Visitor browses shared content
- **WHEN** a client performs a visitor share operation after public access verification
- **THEN** the client SHALL send the share token using `X-Share-Token`

### Requirement: Token Refresh Integration
Clients SHALL recover from access-token expiry by refreshing tokens once and retrying the original owner request when refresh is possible.

#### Scenario: Access token expired
- **WHEN** a client receives an access-token expiry response and has a valid refresh token
- **THEN** the client SHALL refresh the token and retry the original request once

### Requirement: Client Upload Workflow
Clients SHALL follow the backend upload lifecycle for full-file upload behavior.

#### Scenario: Client uploads a file
- **WHEN** a client uploads a local file
- **THEN** the client SHALL initialize upload, upload required chunks unless instant upload succeeds, and complete or cancel the upload according to backend responses

### Requirement: Cross-Client Behavior Consistency
Client implementations SHALL present backend capabilities without redefining backend business rules independently.

#### Scenario: Backend rejects operation
- **WHEN** the backend rejects a client operation with a domain error
- **THEN** the client SHALL surface or adapt that error without assuming a conflicting business outcome

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
