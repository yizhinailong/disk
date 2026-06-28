## MODIFIED Requirements

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

## ADDED Requirements

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
