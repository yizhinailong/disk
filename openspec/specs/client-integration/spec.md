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

#### Scenario: Web owner share management requested
- **WHEN** the Web client performs owner share management such as listing, inspecting, updating, or cancelling shares
- **THEN** the Web client SHALL send the owner access token using `Authorization: Bearer <access_token>`

### Requirement: Visitor Share Domain
Clients SHALL use share tokens for visitor share browse and download flows, and SHALL preserve successful shared-file downloads as binary responses.

#### Scenario: Visitor browses shared content
- **WHEN** a client performs a visitor share operation after public access verification
- **THEN** the client SHALL send the share token using `X-Share-Token`

#### Scenario: Visitor downloads shared content
- **WHEN** a client downloads shared file content after public access verification
- **THEN** the client SHALL send the share token using `X-Share-Token` and handle a successful response as binary content rather than a JSON envelope

### Requirement: Token Refresh Integration
Clients SHALL recover from access-token expiry by refreshing tokens once and retrying the original owner request when refresh is possible, and SHALL settle all callers waiting on a shared refresh attempt whether that refresh succeeds or fails.

#### Scenario: Access token expired
- **WHEN** a client receives an access-token expiry response and has a valid refresh token
- **THEN** the client SHALL refresh the token and retry the original request once

#### Scenario: Concurrent owner requests wait for refresh
- **WHEN** multiple Web owner requests receive access-token expiry while a refresh request is already in progress
- **THEN** the Web client SHALL queue those requests behind the in-flight refresh attempt instead of starting duplicate refresh requests

#### Scenario: Shared refresh succeeds
- **WHEN** a queued Web owner request is waiting and the shared refresh attempt succeeds
- **THEN** the Web client SHALL retry the queued request with the refreshed access token and settle the queued request with the retry result

#### Scenario: Shared refresh fails
- **WHEN** a queued Web owner request is waiting and the shared refresh attempt fails
- **THEN** the Web client SHALL reject the queued request with an authentication error, clear invalid local token state, and avoid leaving the request promise pending

### Requirement: Client Upload Workflow
Clients SHALL follow the backend upload lifecycle for full-file upload behavior, including backend-compatible chunk upload and completion requests.

#### Scenario: Client uploads a file
- **WHEN** a client uploads a local file
- **THEN** the client SHALL initialize upload, upload required chunks unless instant upload succeeds, and complete or cancel the upload according to backend responses

#### Scenario: Web client uploads chunks
- **WHEN** the Web client uploads a file that requires chunk transfer
- **THEN** the Web client SHALL send each chunk using the backend upload task identifier, required chunk index, binary chunk payload, and owner bearer authentication expected by the backend

### Requirement: Cross-Client Behavior Consistency
Client implementations SHALL present backend capabilities without redefining backend business rules independently.

#### Scenario: Backend rejects operation
- **WHEN** the backend rejects a client operation with a domain error
- **THEN** the client SHALL surface or adapt that error without assuming a conflicting business outcome

### Requirement: Authenticated Client Downloads
Clients SHALL apply owner access-token refresh and single retry behavior to authenticated owner download requests, including raw/binary download paths that do not use the normal JSON API interceptor.

#### Scenario: Web owner download uses expired token
- **WHEN** the Web client starts or resumes an owner file download and the current access token is expired while a refresh token is available
- **THEN** the Web client SHALL refresh the owner token and retry the download request once with the refreshed access token

#### Scenario: TUI owner download uses expired token
- **WHEN** the TUI client starts or resumes an owner file download and the current access token is expired while a refresh token is available
- **THEN** the TUI client SHALL refresh the owner token and retry the download request once with the refreshed access token

#### Scenario: Download refresh fails
- **WHEN** an authenticated owner download request cannot refresh an expired or invalid access token
- **THEN** the client SHALL fail the download with an authentication error instead of continuing with stale credentials or hanging

#### Scenario: Visitor share download remains isolated
- **WHEN** a client downloads shared content using a visitor share token
- **THEN** the client SHALL use the share-token authentication domain and SHALL NOT refresh or attach owner JWT credentials for that visitor download
