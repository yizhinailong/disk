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

### Requirement: Typed Upload Completion Correlation
The system SHALL propagate upload-completion correlation explicitly across controller, service, lifecycle, database, and storage coroutine boundaries without thread-local request state or message parsing.

#### Scenario: Completion acquires and advances a finalize lease
- **WHEN** a valid upload-completion request acquires a PostgreSQL finalize lease and advances it through one or more compare-and-set renewals
- **THEN** its application events SHALL retain the response request ID, handling instance, `upload_complete` operation, and upload ID, SHALL add the lease owner only after successful acquisition, and SHALL update the state version from each successful database result

#### Scenario: Completion commits or replays a completed task
- **WHEN** finalization commits or a later request replays the completed result
- **THEN** post-commit events SHALL use the persisted completed state version and a null lease owner because the completion update clears the lease

#### Scenario: Completion has not observed a durable job ID
- **WHEN** completion creates or rearms a cleanup or reconciliation job through an interface that does not return its persistent row ID
- **THEN** the completion event SHALL keep `job_id` null and SHALL NOT derive an ID from the dedupe key, aggregate ID, or message text

### Requirement: Typed Durable Worker Correlation
The system SHALL derive durable Worker correlation only from claimed PostgreSQL job records and SHALL use a bounded operation vocabulary without message parsing or inferred identifiers.

#### Scenario: Worker executes a claimed durable job
- **WHEN** a Worker starts, renews, or persists the outcome of a job returned by the database claim operation
- **THEN** its owned events SHALL use the positive persistent job ID, claimed owner, actual Worker instance, and a fixed operation derived from a known job type, while request ID and state version remain null when the job record does not carry them

#### Scenario: Worker correlates staging cleanup to an upload
- **WHEN** a claimed `staging_cleanup` job has a valid cleanup payload whose non-empty upload ID exactly matches its aggregate ID
- **THEN** Worker events SHALL carry that value as the upload ID, while malformed staging jobs and every other job type SHALL keep the upload ID null

#### Scenario: Worker no longer owns the completed attempt
- **WHEN** the Worker persists a succeeded, retry, or dead-letter result, or can no longer confirm continued ownership
- **THEN** the execution-completed event SHALL retain the persistent job ID and instance but SHALL use a null lease owner

### Requirement: Bounded High-Volume Logging
The system SHALL apply an outcome-based logging policy to upload-chunk traffic so that the normal production log level does not emit one success record per chunk while failures and low-cardinality metrics remain complete.

#### Scenario: Upload chunk succeeds at the production log level
- **WHEN** an upload-chunk request succeeds while the application logger is running at `INFO`
- **THEN** request-detail and success-summary records SHALL remain `DEBUG`-only and the unsampled chunk counters, bytes, and latency metrics SHALL still be updated

#### Scenario: Upload chunk fails at the production log level
- **WHEN** an upload-chunk request fails validation, storage, or persistence while the application logger is running at `INFO`
- **THEN** the failure SHALL be emitted at `INFO` or a higher severity without probabilistic sampling

#### Scenario: Temporary chunk debugging is enabled
- **WHEN** an operator temporarily lowers an isolated API instance to `DEBUG`
- **THEN** successful chunk details MAY be emitted only for the bounded diagnostic window and SHALL continue to exclude credentials and file content

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
- **THEN** health SHALL report `draining=true`, the configured claiming gauge SHALL remain one while the current acceptance gauge becomes zero, readiness SHALL fail, and logs SHALL distinguish completed drain from deadline expiry

### Requirement: Rollback Drain Visibility
Public health output SHALL expose the startup-frozen upload-task creation value and the current count of accepted in-flight business requests without counting health or metrics probes. These fields SHALL be observational and SHALL NOT make readiness unhealthy solely because creation is disabled.

#### Scenario: A compatible API is ready behind frozen upload ingress
- **WHEN** an API started with upload-task creation disabled has completed all previously accepted business requests
- **THEN** readiness SHALL remain healthy when dependencies are healthy and SHALL report `upload_task_creation_enabled=false` and `business_requests_inflight=0`
