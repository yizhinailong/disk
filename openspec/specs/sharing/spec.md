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
The system SHALL allow public visitors to access a share only after successful share validation and password verification when required.

#### Scenario: Share access succeeds
- **WHEN** a visitor provides valid access information for an active share
- **THEN** the system SHALL issue a short-lived share access token scoped to that share

#### Scenario: Share access fails
- **WHEN** a share is missing, expired, cancelled, or password verification fails
- **THEN** the system SHALL reject public access

### Requirement: Share Token Scope
The system SHALL encode a JSON-object `scope` claim in every share token. The scope SHALL contain the external share identifier as string `share_id` and the issued permission as string `permission`; allowed permission values are exactly `view` and `download`. The scoped `share_id` SHALL match the token's top-level `share_code` claim.

#### Scenario: Share token used for matching share
- **WHEN** a visitor uses a valid share token for its associated share
- **THEN** the system SHALL allow only operations permitted by that token scope

#### Scenario: Share token scope is malformed
- **WHEN** a share token has no scope, a non-object scope, missing or mistyped scope fields, an unsupported permission, or a scoped share identifier different from its top-level share identifier
- **THEN** the system SHALL reject the token as malformed

#### Scenario: View scope operations
- **WHEN** a visitor uses a `view` scoped token
- **THEN** the system SHALL allow share browsing and SHALL reject download metadata, content download, and save-to-drive operations

#### Scenario: Download scope operations
- **WHEN** a visitor uses a `download` scoped token
- **THEN** the system SHALL allow browsing, download metadata, content download, and save-to-drive subject to the current live share record and any owner authentication required by save-to-drive

#### Scenario: Share permission changes after token issue
- **WHEN** a share permission changes after a token was issued
- **THEN** the token scope SHALL remain an immutable capability ceiling while the current database permission SHALL be enforced as a live capability floor

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
- **THEN** the system SHALL reject browse, download metadata, content download, and save-to-drive requests for that share by rechecking live share status and expiry on each operation

#### Scenario: Cancellation invalidates all existing tokens
- **WHEN** an owner cancels a share without enumerating or blacklisting each issued token
- **THEN** every existing token SHALL become unusable through the live share-status check

#### Scenario: Visitor download uses owner token instead of share token
- **WHEN** a visitor shared-file download request provides an owner bearer token but no valid share token
- **THEN** the system SHALL reject the request as outside the visitor share access domain
