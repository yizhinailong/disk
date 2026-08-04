# Admin Operations Specification

## Purpose

Defines administrator-only management behavior, safety rules, moderation workflows, and administrative system visibility.

## Requirements

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
- **THEN** the system SHALL return matching platform shares identified only by string external `share_id`, without exposing or requiring internal database primary keys

#### Scenario: Administrator inspects share
- **WHEN** an administrator requests share detail using its external `share_id`
- **THEN** the system SHALL locate the share by external identifier and return that identifier without an internal ID or parallel `share_code` field

#### Scenario: Administrator force-cancels share
- **WHEN** an administrator cancels a target share using its external `share_id`
- **THEN** the system SHALL make that share unavailable for future access

### Requirement: Administrative System Statistics
The system SHALL allow administrators to inspect global storage and system overview statistics.

#### Scenario: Administrator requests overview
- **WHEN** an administrator requests global system statistics
- **THEN** the system SHALL return aggregate user, file, share, storage, and health information available to administrators

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
