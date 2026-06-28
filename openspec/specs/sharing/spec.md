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
