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

### Requirement: Typed System Information Correlation
The system SHALL classify only the exact authenticated system-information path as the bounded `system_info` operation and SHALL propagate its correlation explicitly by value across the controller, service, request stage timer, and storage-statistics error boundary.

#### Scenario: User reads system information
- **WHEN** an authenticated request enters `/api/system/info`
- **THEN** its response, controller events, request stage-timer event, and system-service events SHALL retain the same request ID, actual handling instance, and `system_info` operation

#### Scenario: System-information HTTP completion is emitted
- **WHEN** a system-information response emits its HTTP completion event at the configured log level
- **THEN** that event SHALL retain the response request ID, actual handling instance, and `system_info` operation; successful completion remains `DEBUG` while non-2xx completion remains `WARN`

#### Scenario: System information has no upload or durable-job ownership
- **WHEN** system information records a user, connection-pool size, aggregate count, or stage duration
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those system-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: System-information classification remains bounded
- **WHEN** a system path is not exactly `/api/system/info`, including an extra suffix
- **THEN** the system-information classifier SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

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

### Requirement: Typed File Query Correlation
The system SHALL classify file-list, numeric file-detail, and file-search requests as the bounded `file_query` operation and SHALL propagate their correlation explicitly by value across controller and query-service coroutine boundaries.

#### Scenario: User lists, inspects, or searches files
- **WHEN** an authenticated request enters a file-list, numeric file-detail, or file-search route
- **THEN** its response, HTTP completion event, controller events, and query-service events SHALL retain the same request ID, actual handling instance, and `file_query` operation

#### Scenario: File query has no upload or durable-job ownership
- **WHEN** a file query records identifiers such as a file ID, parent folder ID, or search keyword
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those query values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Another file-domain route is classified
- **WHEN** the request targets upload, download, or a file-mutation route
- **THEN** the file-query classifier SHALL NOT absorb it, and the route SHALL retain its existing bounded operation classification

### Requirement: Typed File Mutation Correlation
The system SHALL classify numeric file rename, drive move, drive copy, and file soft-delete requests as the bounded `file_mutation` operation and SHALL propagate their correlation explicitly by value across controller, mutation-service, and move-to-trash coroutine boundaries.

#### Scenario: User renames, moves, copies, or soft-deletes drive items
- **WHEN** an authenticated request enters a numeric rename, move, copy, or supported soft-delete route
- **THEN** its response, HTTP completion event, controller events, mutation-service events, and delete subflow events SHALL retain the same request ID, actual handling instance, and `file_mutation` operation

#### Scenario: File mutation has no upload or durable-job ownership
- **WHEN** a file mutation records file, folder, content, user, quota, or cache identifiers
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those mutation values SHALL NOT be overloaded into typed correlation fields

#### Scenario: File mutation classification remains bounded
- **WHEN** the request targets file query, upload, download, a nonnumeric rename path, or an unrecognized file-domain path
- **THEN** the file-mutation classifier SHALL NOT absorb it, and the route SHALL retain its existing bounded operation classification

### Requirement: Typed Folder Correlation
The system SHALL classify exact folder-tree and numeric breadcrumb requests as the bounded `folder_query` operation, SHALL classify exact folder-create and numeric folder-rename requests as the bounded `folder_mutation` operation, and SHALL propagate their correlation explicitly by value across controller, folder-DTO, and folder-service boundaries.

#### Scenario: User queries or mutates folders
- **WHEN** an authenticated request enters folder tree, numeric breadcrumb, folder create, or numeric folder rename
- **THEN** its response, HTTP completion event, controller events, direct folder-DTO events, and folder-service events SHALL retain the same request ID, actual handling instance, and matching `folder_query` or `folder_mutation` operation

#### Scenario: Folder request has no upload or durable-job ownership
- **WHEN** a folder request records folder, parent, user, path, transaction, or cache identifiers
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those folder values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Folder classification remains bounded
- **WHEN** a folder path has a nonnumeric ID, an extra suffix, or is not one of the four recognized routes
- **THEN** the folder classifiers SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

### Requirement: Typed Trash Correlation
The system SHALL classify the exact trash-list, restore, permanent-delete compatibility, and empty-all paths as the single bounded `trash` operation because list and permanent delete share `/api/trash`, and SHALL propagate their correlation explicitly by value across controller, trash-DTO, and trash-service boundaries.

#### Scenario: User reads or mutates trash
- **WHEN** an authenticated request enters trash list, batch restore, either permanent-delete compatibility route, or empty-all
- **THEN** its response, controller events, direct trash-DTO events when present, and trash-service events SHALL retain the same request ID, actual handling instance, and `trash` operation

#### Scenario: Trash HTTP completion is emitted
- **WHEN** a trash response emits its HTTP completion event at the configured log level
- **THEN** that event SHALL retain the response request ID, actual handling instance, and `trash` operation; successful completion remains `DEBUG` while non-2xx completion remains `WARN`

#### Scenario: Trash request has no upload or durable-job ownership
- **WHEN** a trash request records trash, user, file, folder, content, path, quota, transaction, or cache identifiers
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those trash-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Trash classification remains bounded
- **WHEN** a trash path has an extra suffix or is not one of `/api/trash`, `/api/trash/restore`, `/api/trash/delete`, and `/api/trash/all`
- **THEN** the trash classifier SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

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

#### Scenario: Local storage assembles staged chunks
- **WHEN** local storage admits and completes or fails an upload assembly across its blocking filesystem queue
- **THEN** every assembly lifecycle event SHALL retain the caller-supplied request, operation, upload, job, owner, and state-version values exactly, including null values, without reconstructing them from the session, paths, or message text

### Requirement: Typed Upload Cancellation Correlation
The system SHALL propagate upload-cancellation correlation explicitly across controller, service, lifecycle, and PostgreSQL transaction boundaries while preserving the public idempotent response contract.

#### Scenario: Cancellation wins the in-progress transition
- **WHEN** an upload-cancellation request atomically changes an owned task from in progress to cancelled
- **THEN** the transition SHALL increment the persistent state version exactly once, and post-commit events SHALL retain the response request ID, handling instance, `upload_cancel` operation, upload ID, and returned state version while job ID and lease owner remain null

#### Scenario: Cancellation replays a cancelled task
- **WHEN** a later cancellation request observes the same task already cancelled
- **THEN** it SHALL return the same public success response, SHALL record a replay using the persisted state version, and SHALL NOT advance that version, release quota again, or enqueue another cleanup job

### Requirement: Typed Durable Worker Correlation
The system SHALL derive durable Worker correlation only from claimed PostgreSQL job records and SHALL use a bounded operation vocabulary without message parsing or inferred identifiers.

#### Scenario: Worker executes a claimed durable job
- **WHEN** a Worker starts, renews, or persists the outcome of a job returned by the database claim operation
- **THEN** its owned events SHALL use the positive persistent job ID, claimed owner, actual Worker instance, and a fixed operation derived from a known job type, while request ID and state version remain null when the job record does not carry them

#### Scenario: Worker correlates staging cleanup to an upload
- **WHEN** a claimed `staging_cleanup` job has a valid cleanup payload whose non-empty upload ID exactly matches its aggregate ID
- **THEN** job-level Worker events SHALL carry that value as the upload ID, while malformed staging jobs and every other job type SHALL keep the job-level upload ID null; an expiration lifecycle event MAY add an upload ID and state version only from the actual successful upload-task transition

#### Scenario: Worker no longer owns the completed attempt
- **WHEN** the Worker persists a succeeded, retry, or dead-letter result, or can no longer confirm continued ownership
- **THEN** the execution-completed event SHALL retain the persistent job ID and instance but SHALL use a null lease owner

### Requirement: Typed Download Correlation
The system SHALL propagate download request correlation explicitly across owner and visitor controllers, database query services, the shared response builder, integrity handling, delayed stream callbacks, statistics, and share audit boundaries without thread-local request state.

#### Scenario: Owner or visitor requests download metadata or content
- **WHEN** an owner or visitor download route handles a request
- **THEN** its domain events SHALL use the response request ID, actual handling instance, and fixed `download` operation, while upload ID, job ID, lease owner, and state version remain null

#### Scenario: Download detects final Blob inconsistency
- **WHEN** preflight, stream opening, or streaming detects a missing, mismatched, or interrupted final Blob
- **THEN** responder and integrity events SHALL retain the request context, and the reconciliation finding details SHALL persist the same non-empty request ID and `download` operation

#### Scenario: Visitor download writes its audit result
- **WHEN** a visitor download records its final HTTP outcome
- **THEN** the share-download audit details SHALL persist the same request ID and `download` operation without recording the share token or overloading upload or job identifiers

### Requirement: Typed Cleanup Correlation
The system SHALL classify the manual expired-cleanup endpoint as the bounded `cleanup` operation and SHALL propagate correlation explicitly by value through cleanup controller, service, database, trash, content-reference, and upload-lifecycle coroutine boundaries.

#### Scenario: Administrator runs expired cleanup
- **WHEN** an administrator calls `POST /api/admin/maintenance/cleanup/expired` with a valid request ID
- **THEN** the response, HTTP completion event, controller event, and cleanup batch events SHALL use the same request ID, actual handling instance, and `cleanup` operation, while batch-level upload ID, job ID, lease owner, and state version remain null

#### Scenario: Cleanup expires an individual upload
- **WHEN** cleanup reads and expires a persistent upload-task row
- **THEN** the winning transition SHALL increment the persistent state version exactly once, and its post-commit item-level lifecycle event SHALL add that row's actual upload ID and returned state version without changing the caller's request or durable-job correlation

#### Scenario: Cleanup enqueues Blob garbage collection without observing its row ID
- **WHEN** expired-trash deletion decrements a content reference and the repository enqueue interface does not return the durable job's persistent row ID
- **THEN** request-side cleanup events SHALL keep the job ID null and SHALL NOT derive it from a content ID, aggregate ID, dedupe key, count, or message text

#### Scenario: Worker executes expiration through shared services
- **WHEN** a claimed expired-upload or expired-trash job enters the same lifecycle and trash/content services
- **THEN** its job-level events SHALL retain the corresponding `storage_job_expire_uploads` or `storage_job_expire_trash` operation, persistent job ID, and current owner while request ID and state version remain null; an upload item-level event MAY add only the state version returned by its successful expiration transition

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

### Requirement: Typed S3 SDK Correlation
The system SHALL propagate caller-owned correlation explicitly by value through storage abstractions, the S3 blocking queue, and the AWS adapter without thread-local state or identifier inference.

#### Scenario: API or Worker reaches the S3 adapter
- **WHEN** an upload, download, cleanup, or claimed durable Worker operation performs one or more S3 SDK calls
- **THEN** every final SDK result event SHALL retain the caller's typed request, operation, upload, job, owner, and state-version values exactly as supplied, including null values for unavailable identifiers

#### Scenario: AWS SDK call returns a final result
- **WHEN** an AWS SDK call completes after its configured internal retries
- **THEN** the adapter SHALL record a bounded SDK operation and outcome, SHALL emit success, not-found, and conflict only at `DEBUG`, SHALL emit every other final outcome at `WARN`, and SHALL keep dependency metrics unsampled

#### Scenario: S3 result event is serialized
- **WHEN** the adapter records an SDK result event
- **THEN** it SHALL NOT include bucket, endpoint, object key or prefix, remote multipart upload ID, continuation token, credentials, signatures, object content, or SDK exception text

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
