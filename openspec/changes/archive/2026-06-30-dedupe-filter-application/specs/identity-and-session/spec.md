## MODIFIED Requirements

### Requirement: Access Token Authentication
The system SHALL protect non-public APIs using JWT bearer access tokens declared through each protected route's request filter configuration.

#### Scenario: Protected API without token
- **WHEN** a client calls a protected API without a valid bearer access token
- **THEN** the system SHALL reject the request with an authentication error

#### Scenario: Protected API with valid token
- **WHEN** a client calls a protected API with a valid access token
- **THEN** the system SHALL associate the request with the authenticated user

#### Scenario: Protected route filter declaration reviewed
- **WHEN** a backend route is intended to require an authenticated user
- **THEN** the route SHALL declare JWT bearer authentication in its route-level filter list
