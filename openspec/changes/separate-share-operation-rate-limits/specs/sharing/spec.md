## ADDED Requirements

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
- **WHEN** a browse or download operation has a missing, malformed, expired, revoked, route-mismatched, or insufficient-scope Share Token
- **THEN** the request SHALL retain its existing authentication or authorization response
- **AND** it SHALL NOT consume a browse or download counter

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
