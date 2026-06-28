## MODIFIED Requirements

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
