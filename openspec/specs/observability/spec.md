# Observability Specification

## Purpose

Defines health, system information, operation logging, tracing, and maintenance visibility behavior.

## Requirements

### Requirement: Health Check
The system SHALL expose a public health check that reports overall service health and component status.

#### Scenario: Components are healthy
- **WHEN** the health endpoint can verify required components are healthy
- **THEN** the system SHALL return healthy status information

#### Scenario: Component is unhealthy
- **WHEN** a required component health check fails
- **THEN** the system SHALL report degraded or unhealthy status with component details

### Requirement: System Information
The system SHALL expose authenticated system information for operational visibility.

#### Scenario: Authenticated system info request
- **WHEN** an authenticated user requests system information
- **THEN** the system SHALL return version, runtime, connection, and storage summary information available to that user

### Requirement: Operation Logs
The system SHALL record and expose user-visible operation logs for key actions.

#### Scenario: User lists operation logs
- **WHEN** an authenticated user requests operation logs
- **THEN** the system SHALL return that user's operation log entries with pagination

#### Scenario: Key operation occurs
- **WHEN** a tracked operation such as login, upload, download, delete, share, restore, or administrator action occurs
- **THEN** the system SHALL record operation log information sufficient for audit and display

### Requirement: Request Trace Visibility
The system SHALL associate requests with trace identifiers for log correlation and response visibility.

#### Scenario: Request is handled
- **WHEN** the system handles an HTTP request
- **THEN** it SHALL make the request trace identifier available for logging and response propagation

### Requirement: Background Maintenance Visibility
The system SHALL expose Worker claiming configuration and current acceptance state independently, and a Worker in observation mode SHALL continue to expose dependency readiness and database-backed queue snapshots without executing maintenance work.

#### Scenario: Scheduled cleanup runs
- **WHEN** scheduled maintenance executes
- **THEN** the system SHALL process expired upload or trash state according to the relevant lifecycle rules

#### Scenario: Worker observes without claiming
- **WHEN** a Worker starts with job claiming disabled and its required dependencies are healthy
- **THEN** readiness SHALL succeed, health SHALL report claiming disabled and accepting false, queue snapshot collection SHALL continue, and claiming/acceptance gauges SHALL both be zero

#### Scenario: Claiming Worker drains
- **WHEN** a claiming Worker begins graceful shutdown
- **THEN** the configured claiming gauge SHALL remain one while the current acceptance gauge becomes zero and readiness fails

### Requirement: Rollback Drain Visibility
Public health output SHALL expose the startup-frozen upload-task creation value and the current count of accepted in-flight business requests without counting health or metrics probes. These fields SHALL be observational and SHALL NOT make readiness unhealthy solely because creation is disabled.

#### Scenario: A compatible API is ready behind frozen upload ingress
- **WHEN** an API started with upload-task creation disabled has completed all previously accepted business requests
- **THEN** readiness SHALL remain healthy when dependencies are healthy and SHALL report `upload_task_creation_enabled=false` and `business_requests_inflight=0`
