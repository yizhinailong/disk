# Identity And Session Specification

## Purpose

Defines user identity, authentication, token lifecycle, logout, revocation, and account protection behavior.

## Requirements

### Requirement: User Registration
The system SHALL allow new users to register with a unique username and email address and SHALL initialize default account storage state.

#### Scenario: Registration succeeds
- **WHEN** a user submits valid and unique registration information
- **THEN** the system SHALL create the user account and return the created user information

#### Scenario: Duplicate identity
- **WHEN** a registration request uses an existing username or email
- **THEN** the system SHALL reject the request with the corresponding identity conflict error

### Requirement: Password Protection
The system SHALL store passwords using secure password hashing and SHALL never persist plaintext passwords.

#### Scenario: Password is stored
- **WHEN** a user registers or changes password
- **THEN** the system SHALL store only a password hash suitable for password verification

### Requirement: User Login
The system SHALL authenticate users by username or email plus password and return access and refresh tokens after successful authentication.

#### Scenario: Login succeeds
- **WHEN** a user provides valid credentials for an enabled account
- **THEN** the system SHALL return an access token, refresh token, expiration information, and user summary

#### Scenario: Login fails
- **WHEN** a user provides invalid credentials or the account is unavailable
- **THEN** the system SHALL reject the login with an authentication error

### Requirement: Access Token Authentication
The system SHALL protect non-public APIs using JWT bearer access tokens enforced by global authentication with explicit public route exemptions.

#### Scenario: Protected API without token
- **WHEN** a client calls a protected API without a valid bearer access token
- **THEN** the system SHALL reject the request with an authentication error

#### Scenario: Protected API with valid token
- **WHEN** a client calls a protected API with a valid access token
- **THEN** the system SHALL associate the request with the authenticated user

#### Scenario: Protected route authentication ownership reviewed
- **WHEN** a backend route is intended to require an authenticated user
- **THEN** the route SHALL be covered by the global JWT authentication filter and SHALL NOT declare a duplicate route-level JWT authentication filter

#### Scenario: Public route exemption reviewed
- **WHEN** a backend route is intentionally public, such as registration, login, refresh, health, or public share access
- **THEN** the route SHALL be explicitly exempted from global JWT authentication and SHALL NOT require a bearer access token

### Requirement: Refresh Token Lifecycle
The system SHALL support refresh-token-based access token renewal and SHALL prevent invalid, expired, revoked, or already-used refresh tokens from being accepted.

#### Scenario: Refresh succeeds
- **WHEN** a client presents a valid refresh token
- **THEN** the system SHALL issue a new access token and any required refresh token update

#### Scenario: Refresh token is invalid
- **WHEN** a client presents an invalid, expired, revoked, or already-used refresh token
- **THEN** the system SHALL reject the refresh request

#### Scenario: Refresh races across API instances
- **WHEN** the same valid refresh token is submitted concurrently to two API instances sharing Redis
- **THEN** exactly one request SHALL atomically rotate the stored token, while the other request and every replay of the old token SHALL be rejected

#### Scenario: Redis is unavailable during refresh
- **WHEN** two API instances attempt to refresh the same valid token while their Redis connection is unavailable
- **THEN** neither request SHALL produce an authoritative rotation and both SHALL return the Redis operation failure
- **AND** after Redis recovers, the original token SHALL still rotate exactly once and then reject replay

### Requirement: Logout Revocation
The system SHALL revoke tokens during logout so that logged-out credentials cannot continue to authorize protected APIs.

#### Scenario: Logout succeeds
- **WHEN** an authenticated user logs out
- **THEN** the system SHALL invalidate the relevant token state and return success

#### Scenario: Logout propagates across API instances
- **WHEN** API instance A successfully logs out an access token and the next protected request reaches API instance B
- **THEN** instance B SHALL immediately reject the token, including after B restarts and loses all process-local revocation cache state

### Requirement: Revocation checks fail closed during Redis faults
Access-token and share-token revocation checks SHALL treat an unavailable shared Redis service as an authentication dependency failure, not as evidence that a token is live. Rate-limit accounting MAY retain its separately documented fail-open policy.

#### Scenario: Redis revocation lookup is unavailable
- **WHEN** a live access token or structurally valid share token reaches either of two API instances while Redis revocation lookup is unavailable
- **THEN** each instance SHALL return the Redis operation failure and SHALL NOT enter the protected business operation

#### Scenario: Redis revocation lookup recovers
- **WHEN** Redis connectivity returns after a temporary fault
- **THEN** the original API processes SHALL resume token validation without restart, preserve persisted revocations, and SHALL NOT retain a permanent negative or failure cache

### Requirement: Redis-backed session security state is durable
The current refresh-token hash and unexpired access/share-token revocations SHALL be treated as expiring session security state rather than disposable cache. A supported Redis restart or failover SHALL preserve acknowledged security-state writes and their remaining TTL. File-list caches and rate-limit counters MAY follow their separately documented rebuild or degradation policy.

#### Scenario: Persistent Redis restarts
- **WHEN** Redis acknowledges a refresh-token state and an access-token revocation, then restarts from its configured durable storage
- **THEN** both keys SHALL remain present with positive TTLs that have not been reset
- **AND** a cold API instance SHALL reject the revoked access token while two API instances racing the retained refresh token SHALL still select exactly one rotation winner

#### Scenario: Replicated Redis writer is promoted
- **WHEN** the old Redis writer is fenced after the current refresh-token hash and access-token revocation have reached its replica, and that replica is promoted behind the same application endpoint
- **THEN** the existing API processes SHALL reconnect without restart, the revoked token SHALL remain rejected, and the retained refresh token SHALL still select exactly one Lua/CAS rotation winner across two API instances
- **AND** replicated expiring keys SHALL retain positive TTLs that continue from their original absolute expiry rather than being reset at promotion

#### Scenario: Redis security state cannot be trusted after disaster recovery
- **WHEN** Redis recovery cannot prove preservation of every acknowledged, unexpired session-security write
- **THEN** authentication traffic SHALL remain closed until the JWT signing secret is rotated and every API instance is restarted with the new secret
- **AND** all previously issued access, refresh, and share tokens SHALL require reauthentication or reissuance before traffic reopens

### Requirement: Account Protection
The system SHALL enforce account status and rate-limit protections for authentication-sensitive flows. Consecutive password failures and temporary account lock deadlines SHALL be owned by PostgreSQL so independently clocked API instances cannot lose increments, extend an existing lock, or disagree about expiry. Administrator-disabled and administrator-locked states SHALL remain distinct from temporary password-failure locks.

#### Scenario: Account is disabled or locked
- **WHEN** a disabled or locked account attempts to authenticate
- **THEN** the system SHALL reject the login attempt

#### Scenario: Login attempts exceed allowed rate
- **WHEN** login attempts exceed the configured protection threshold
- **THEN** the system SHALL temporarily reject further attempts

#### Scenario: Password failures race across API instances
- **WHEN** invalid-password requests for one active account execute concurrently across two API instances
- **THEN** PostgreSQL SHALL atomically accumulate at most the first five consecutive failures, the fifth SHALL set one 15-minute deadline from database `NOW()` without changing the administrator-owned status, and requests observing the live deadline SHALL return `AccountLocked` without extending it

#### Scenario: Temporary account lock expires
- **WHEN** PostgreSQL time reaches the temporary lock deadline and the next request supplies the correct password
- **THEN** the successful-login conditional update SHALL clear the failure count and deadline before tokens are issued, and every API instance SHALL observe the account as active

#### Scenario: Legacy temporary lock expires
- **WHEN** a compatibility row has administrator-lock status together with a non-null expired deadline from the former automatic-lock implementation
- **THEN** correct authentication SHALL normalize it once to active status while clearing the failure state, but an administrator lock with no deadline SHALL remain locked

#### Scenario: Refresh observes account protection
- **WHEN** a refresh token belongs to a disabled, administrator-locked, or currently temporary-locked account
- **THEN** refresh SHALL use PostgreSQL time and reject the request without rotating the token
