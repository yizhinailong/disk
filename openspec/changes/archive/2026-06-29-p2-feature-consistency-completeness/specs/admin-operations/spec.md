## ADDED Requirements

### Requirement: Administrator User Storage Management
The system SHALL allow administrators to update supported user storage quota values through administrator operations while enforcing authorization, target-user validation, quota consistency, and administrative result recording.

#### Scenario: Administrator updates user storage quota
- **WHEN** an authenticated administrator updates another user's storage quota to a valid value that is not lower than required storage consistency rules allow
- **THEN** the system SHALL persist the new quota and return the updated administrative user storage information

#### Scenario: Storage quota update is invalid
- **WHEN** an administrator submits an invalid storage quota value, targets a missing user, or requests a quota that conflicts with the user's current storage usage
- **THEN** the system SHALL reject the operation without changing the user's storage values

#### Scenario: Non-administrator updates user storage quota
- **WHEN** a non-administrator attempts to update a user's storage quota through administrator operations
- **THEN** the system SHALL reject the request with an administrator authorization error

#### Scenario: Administrator storage update is recorded
- **WHEN** an administrator storage quota update succeeds or fails with a domain-level administrative result
- **THEN** the system SHALL make the result observable through the same administrative auditing or operation-result mechanism used for user management operations
