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

#### Scenario: Share identifier is generated
- **WHEN** the system creates an external share identifier
- **THEN** it SHALL use libsodium's cryptographically secure uniform random source to select exactly eight characters from the existing ASCII alphanumeric alphabet
- **AND** the identifier format, database uniqueness constraint, API fields, links, and client handling SHALL remain unchanged

#### Scenario: Generated share identifier collides
- **WHEN** the database uniqueness constraint rejects a generated external share identifier because it already exists
- **THEN** the system SHALL generate and atomically try a fresh identifier within the same share-creation transaction
- **AND** it SHALL stop after five total candidates and fail without persisting a share or item association if every candidate collides
- **AND** non-identifier database failures SHALL not be treated as identifier collisions

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

#### Scenario: Cancellation propagates across API instances
- **WHEN** an owner cancels a share through API instance A and an existing share token is next used through API instance B
- **THEN** instance B SHALL reject the token from the shared live share state, including after B restarts and clears all process-local token cache state

#### Scenario: Visitor download uses owner token instead of share token
- **WHEN** a visitor shared-file download request provides an owner bearer token but no valid share token
- **THEN** the system SHALL reject the request as outside the visitor share access domain

### Requirement: Share Operation Rate Limits
The system SHALL enforce independent fixed-window request limits for public share
access, authenticated share browse, and authenticated share download operations.
Access SHALL default to 30 requests per 60 seconds per normalized client IP, browse
SHALL default to 60 requests per 60 seconds per verified Share Token JTI, and
download SHALL default to 10 requests per 60 seconds per verified Share Token JTI.

#### Scenario: Share access reaches its boundary
- **WHEN** one normalized client IP sends requests to `POST /api/share/access/{share_id}` during one configured window
- **THEN** the first 30 requests SHALL continue to share validation and request 31 SHALL return HTTP 429 with business code `10005`
- **AND** successful, rejected, password-protected, and passwordless access attempts SHALL all consume the general access bucket

#### Scenario: General access and password-failure accounting coexist
- **WHEN** a password-protected access attempt is a countable validation failure
- **THEN** it SHALL consume the general `rate:share_access` bucket and independently consume the existing `rate:share_password` failed-validation bucket
- **AND** neither bucket SHALL clear or consume the other

#### Scenario: Share browse reaches its boundary
- **WHEN** a successfully authenticated visitor sends browse requests with one verified Share Token JTI during one configured window
- **THEN** the first 60 requests SHALL continue to browse handling and request 61 SHALL return HTTP 429 with business code `10005`

#### Scenario: Download operations share one boundary
- **WHEN** a successfully authenticated download-scoped Share Token JTI is used for download metadata, binary content, or save-to-drive requests during one configured window
- **THEN** those routes SHALL share one 10-request download bucket and request 11 through any covered route SHALL return HTTP 429 with business code `10005`

#### Scenario: Range and retry requests are counted
- **WHEN** a client sends an initial binary download, a Range resume, or an automatic retry as separate HTTP requests
- **THEN** each request SHALL consume the download bucket exactly once regardless of whether the requests belong to one logical file transfer

#### Scenario: Limiter families and token identities are isolated
- **WHEN** access, browse, and download requests occur in the same window or requests use separately issued Share Token JTIs
- **THEN** each operation family and each distinct JTI SHALL consume only its own counter

#### Scenario: Share Token authentication precedes authenticated limiting
- **WHEN** a browse or download operation has a missing, malformed, expired, revoked, or insufficient-scope Share Token
- **THEN** the request SHALL retain its existing authentication or authorization response
- **AND** it SHALL NOT consume a browse or download counter

#### Scenario: Live share binding remains service owned
- **WHEN** a structurally valid and scope-authorized Share Token is used against a route or live share record that the service rejects
- **THEN** the existing service-layer binding, status, expiry, and current-permission response SHALL be preserved
- **AND** the authenticated operation request MAY consume its browse or download counter before that business response

#### Scenario: Save-to-drive requires both authentication domains
- **WHEN** a save-to-drive request is evaluated
- **THEN** a valid owner bearer token and a valid download-scoped Share Token SHALL both be required before the download counter is consumed
- **AND** failure in either authentication domain SHALL retain its authentication response without consuming the download counter

#### Scenario: Fixed-window lifetime and response contract
- **WHEN** the first request creates an operation counter
- **THEN** the counter SHALL receive the configured window TTL and later increments SHALL NOT refresh its expiry
- **AND** a limited response SHALL include `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After`

#### Scenario: Redis operation limiting is unavailable
- **WHEN** Redis increment or limit checking fails for access, browse, or download
- **THEN** the system SHALL log the failure without credential material and allow the underlying request to continue

#### Scenario: Rate-limit state excludes replayable credentials
- **WHEN** the system constructs keys, logs diagnostics, records audits, or stores test evidence for share operation limiting
- **THEN** it SHALL NOT store or log the raw Share Token, `X-Share-Token`, owner authorization header, share password, or password hash
- **AND** authenticated limiter keys SHALL use only the verified token JTI as their token identity
