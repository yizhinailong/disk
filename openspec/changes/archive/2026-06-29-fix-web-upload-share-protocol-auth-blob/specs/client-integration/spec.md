## MODIFIED Requirements

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

### Requirement: Client Upload Workflow
Clients SHALL follow the backend upload lifecycle for full-file upload behavior, including backend-compatible chunk upload and completion requests.

#### Scenario: Client uploads a file
- **WHEN** a client uploads a local file
- **THEN** the client SHALL initialize upload, upload required chunks unless instant upload succeeds, and complete or cancel the upload according to backend responses

#### Scenario: Web client uploads chunks
- **WHEN** the Web client uploads a file that requires chunk transfer
- **THEN** the Web client SHALL send each chunk using the backend upload task identifier, required chunk index, binary chunk payload, and owner bearer authentication expected by the backend
