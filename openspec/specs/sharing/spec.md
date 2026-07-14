# Sharing Specification

## Purpose

Defines owner-managed shares and public visitor share access, including share identifiers, passwords, share tokens, browse, and download behavior.

## Requirements

### Requirement: Share Creation
The system SHALL allow authenticated users to create shares for files or folders they own.

#### Scenario: Share creation succeeds
- **WHEN** an authenticated user creates a share for accessible owned items with valid options
- **THEN** the system SHALL create a share with an external share identifier, permission, optional password, and expiry metadata

#### Scenario: Share includes inaccessible item
- **WHEN** a user attempts to share an item they do not own or cannot access
- **THEN** the system SHALL reject the share request

### Requirement: External Share Identifier
The system SHALL use an external share identifier for API paths and responses instead of exposing internal database primary keys.

#### Scenario: Share is returned to clients
- **WHEN** the system returns share information through public or owner APIs
- **THEN** the response SHALL identify the share using the external share identifier

### Requirement: Owner Share Management
The system SHALL allow share owners to list, inspect, update, and cancel their own shares, and clients SHALL authenticate owner share management requests with the owner's bearer access token.

#### Scenario: Owner lists shares
- **WHEN** an authenticated user lists their shares with a valid bearer access token
- **THEN** the system SHALL return only shares owned by that user with status and pagination metadata

#### Scenario: Owner share request omits bearer token
- **WHEN** a client calls an owner share management API without the owner's bearer access token
- **THEN** the system SHALL reject the request as unauthenticated

#### Scenario: Owner cancels share
- **WHEN** an owner cancels one or more shares with a valid bearer access token
- **THEN** the system SHALL mark those shares unavailable for future access

### Requirement: Public Share Access
The system SHALL allow public visitors to access an active share only after successful share validation and password verification when required, and SHALL rate-limit countable validation failures without revealing whether the share code or password was invalid.

#### Scenario: Share access succeeds
- **WHEN** a visitor provides the correct password for an active password-protected share, or accesses an active share that has no password
- **THEN** the system SHALL issue a short-lived share access token scoped to that share
- **AND** the system SHALL neither increment nor clear the failed-validation counter

#### Scenario: First five countable validation failures
- **WHEN** a visitor omits the password or provides an empty or wrong password for a password-protected share, or provides a nonexistent share code
- **AND** fewer than five countable failures have already occurred for the supplied share code and normalized client IP during the current window
- **THEN** the system SHALL count the failure using `rate:share_password:{share_code}:{normalized_ip}`
- **AND** the system SHALL return HTTP 400 with code `60003`, message `Share access validation failed`, and `data` set to `null`

#### Scenario: Sixth and later countable validation failures
- **WHEN** five countable failures have already occurred for the supplied share code and normalized client IP during the current window
- **AND** another countable validation failure occurs
- **THEN** the system SHALL return the existing Too Many Requests response with HTTP 429, code `10005`, message `Too many password verification attempts, please try again later`, and `data` set to `null`

#### Scenario: Failed-validation window lifetime
- **WHEN** the first countable validation failure creates its Redis counter
- **THEN** the system SHALL start a fixed 900-second window
- **AND** later failures SHALL increment the counter without refreshing its expiry

#### Scenario: Redis failure during failed-validation accounting
- **WHEN** Redis is unavailable or failed-validation accounting otherwise fails
- **THEN** the system SHALL fail open for rate-limit accounting and continue evaluating the access request
- **AND** a countable validation failure SHALL still receive the unified HTTP 400 response

#### Scenario: Share is expired or cancelled
- **WHEN** a visitor attempts to access an expired or cancelled share
- **THEN** the system SHALL reject public access using the existing expired or cancelled share semantics
- **AND** the response SHALL remain outside the unified failed-validation contract

### Requirement: Share Token Scope
The system SHALL limit share tokens to the associated share and permission.

#### Scenario: Share token used for matching share
- **WHEN** a visitor uses a valid share token for its associated share
- **THEN** the system SHALL allow only operations permitted by that token scope

#### Scenario: Share token used outside scope
- **WHEN** a visitor uses a share token for another share or disallowed operation
- **THEN** the system SHALL reject the request

### Requirement: Browse Shared Content
The system SHALL allow visitors with valid share tokens to browse shared folder content.

#### Scenario: Visitor browses active share
- **WHEN** a visitor browses shared content with a valid token
- **THEN** the system SHALL return shared items and navigation metadata allowed by the share

### Requirement: Download Shared Content
The system SHALL allow visitors with valid share tokens and download permission to download shared files using the binary download contract.

#### Scenario: Visitor downloads shared file
- **WHEN** a visitor requests a shared file with a valid share token and download permission
- **THEN** the system SHALL return the file content using the download contract

#### Scenario: Share is cancelled after token issue
- **WHEN** a share is cancelled or expires after a token was issued
- **THEN** the system SHALL reject browse and download requests for that share

#### Scenario: Visitor download uses owner token instead of share token
- **WHEN** a visitor shared-file download request provides an owner bearer token but no valid share token
- **THEN** the system SHALL reject the request as outside the visitor share access domain
