## ADDED Requirements

### Requirement: Administrator Authorization
The system SHALL restrict administrator APIs to authenticated users with administrator role.

#### Scenario: Administrator accesses admin API
- **WHEN** an authenticated administrator calls an admin API
- **THEN** the system SHALL allow the request to proceed to the administrator operation

#### Scenario: Non-administrator accesses admin API
- **WHEN** a non-administrator calls an admin API
- **THEN** the system SHALL reject the request with an administrator authorization error

### Requirement: User Administration
The system SHALL allow administrators to list, inspect, and manage user status and role according to safety rules.

#### Scenario: Administrator lists users
- **WHEN** an administrator requests the user list with valid filters and pagination
- **THEN** the system SHALL return matching users and pagination metadata

#### Scenario: Administrator changes user status or role
- **WHEN** an administrator changes another user's status or role to a valid value
- **THEN** the system SHALL update the target user and record the administrative result

### Requirement: Administrator Self-Protection
The system SHALL prevent administrators from modifying their own status or role through administrator operations.

#### Scenario: Administrator modifies self
- **WHEN** an administrator attempts to change their own status or role
- **THEN** the system SHALL reject the operation

### Requirement: Last Administrator Protection
The system SHALL prevent operations that would disable, delete, lock, or demote the last active administrator.

#### Scenario: Operation would remove last administrator
- **WHEN** an administrator operation would leave the system without an active administrator
- **THEN** the system SHALL reject the operation

### Requirement: Administrative Share Moderation
The system SHALL allow administrators to inspect platform shares and force-cancel shares.

#### Scenario: Administrator lists shares
- **WHEN** an administrator lists shares with valid filters and pagination
- **THEN** the system SHALL return matching platform shares

#### Scenario: Administrator force-cancels share
- **WHEN** an administrator cancels a target share
- **THEN** the system SHALL make that share unavailable for future access

### Requirement: Administrative System Statistics
The system SHALL allow administrators to inspect global storage and system overview statistics.

#### Scenario: Administrator requests overview
- **WHEN** an administrator requests global system statistics
- **THEN** the system SHALL return aggregate user, file, share, storage, and health information available to administrators
