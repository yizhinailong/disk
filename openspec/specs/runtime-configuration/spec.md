# Runtime Configuration Specification

## Purpose

Defines backend startup, secure configuration, infrastructure connectivity, storage initialization, background tasks, and public route configuration behavior.

## Requirements

### Requirement: Configuration Loading
The system SHALL load runtime configuration before initializing services that depend on configuration values.

#### Scenario: Application starts
- **WHEN** the backend process starts
- **THEN** the system SHALL load the configured runtime settings before service initialization

### Requirement: Secure Configuration Validation
The system SHALL validate security-sensitive configuration before accepting runtime traffic.

#### Scenario: Required secure configuration is missing
- **WHEN** security-sensitive configuration is missing or invalid during startup
- **THEN** the system SHALL fail startup instead of running with unsafe settings

### Requirement: Database Connectivity Configuration
The system SHALL use configured PostgreSQL connection settings for persistent metadata storage.

#### Scenario: Database-backed operation executes
- **WHEN** an operation requires persistent metadata
- **THEN** the system SHALL use the configured PostgreSQL client for that metadata access

### Requirement: Redis Connectivity Configuration
The system SHALL use configured Redis connection settings for token, cache, rate-limit, and temporary state behavior.

#### Scenario: Redis-backed operation executes
- **WHEN** an operation requires Redis-backed state
- **THEN** the system SHALL use the configured Redis client for that state access

### Requirement: File Storage Configuration
The system SHALL initialize file storage from configured storage paths and upload settings.

#### Scenario: Storage initializes
- **WHEN** the backend starts successfully
- **THEN** the system SHALL initialize the active file storage implementation using configured storage and temporary upload paths

### Requirement: Background Task Registration
The system SHALL register background maintenance tasks during application startup.

#### Scenario: Application begins serving
- **WHEN** the backend application enters its serving lifecycle
- **THEN** scheduled cleanup and token maintenance tasks SHALL be registered

### Requirement: Public Route Exemptions
The system SHALL configure public APIs such as registration, login, refresh, health, and public share access so they can execute without bearer-token authentication while still applying any route-specific protection.

#### Scenario: Public route requested
- **WHEN** a client calls a configured public route
- **THEN** the system SHALL allow the route to bypass bearer-token authentication while still applying any route-specific protection

#### Scenario: Route-owned authentication is used
- **WHEN** bearer-token authentication is enforced through route-level filters instead of a global authentication filter
- **THEN** public-route configuration SHALL remain consistent with the route declarations and SHALL NOT be relied on as the primary mechanism for protecting non-public routes
