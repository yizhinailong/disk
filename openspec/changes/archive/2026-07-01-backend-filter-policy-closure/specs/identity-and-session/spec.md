## MODIFIED Requirements

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
