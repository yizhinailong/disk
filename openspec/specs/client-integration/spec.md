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
Clients SHALL use share tokens for visitor share browse and download flows, including full and ranged download requests, and SHALL NOT fall back to owner bearer authentication for visitor transfers.

#### Scenario: Visitor browses shared content
- **WHEN** a client performs a visitor share operation after public access verification
- **THEN** the client SHALL send the share token using `X-Share-Token`

#### Scenario: Visitor resumes shared download
- **WHEN** a client resumes a visitor share download using an HTTP byte range request
- **THEN** the client SHALL send both the `Range` header and the share token using `X-Share-Token`

#### Scenario: Visitor download remains isolated from owner authentication
- **WHEN** a visitor share download or resume request is made from a client that may also have owner session state
- **THEN** the client SHALL authenticate the visitor request with the share token and SHALL NOT require or substitute an owner bearer token

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
