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

### Requirement: Typed User Profile Correlation
The system SHALL classify only the exact authenticated user-profile, password, and storage paths as the bounded `user` operation and SHALL propagate their correlation explicitly by value across the controller, direct user-DTO events, and user-service coroutine boundaries.

#### Scenario: User reads or changes account details
- **WHEN** an authenticated request reads or updates `/api/user/profile`, changes `/api/user/password`, or reads `/api/user/storage`
- **THEN** its response, HTTP completion event at the configured level, controller events, direct user-DTO events, and user-service events SHALL retain the same request ID, actual handling instance, and `user` operation

#### Scenario: Profile methods share one path classification
- **WHEN** either GET or PATCH uses the exact `/api/user/profile` path
- **THEN** both methods SHALL use the same `user` operation because the HTTP metrics classifier receives only the normalized path

#### Scenario: User profile requests have no upload or durable-job ownership
- **WHEN** a user request records account, profile, password-change, quota, or aggregate storage values
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those user-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: User classification remains bounded
- **WHEN** a user path is not exactly `/api/user/profile`, `/api/user/password`, or `/api/user/storage`, including an extra suffix
- **THEN** the user classifier SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

### Requirement: Authentication Filter Correlation
The JWT authentication, share-token authentication, and administrator-authorization filters SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every event directly owned by those filters SHALL use that context without changing authentication, authorization, public-path, scope, revocation, or response behavior.

#### Scenario: Authentication filter handles a traced request
- **WHEN** one of those filters emits an exempt, success, rejection, revocation, scope, or dependency-failure event for a request whose trace attribute has been established
- **THEN** the event SHALL retain the same request ID, actual handling instance, and bounded operation as the HTTP completion event

#### Scenario: Authentication filter observes identity values
- **WHEN** a filter observes a user ID, role, status, token JTI, share code, permission, path, or dependency result
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Filter is invoked without a trace attribute
- **WHEN** a unit, utility, or compatibility caller invokes a filter without an established request ID
- **THEN** the direct filter event SHALL retain the classifier's bounded operation and explicit JSON null request correlation rather than using thread-local or credential-derived state

#### Scenario: Authentication filter handles credentials
- **WHEN** JWT or share-token authentication succeeds or fails
- **THEN** filter events and typed correlation fields SHALL NOT contain raw Authorization or `X-Share-Token` header values, access tokens, refresh tokens, share tokens, passwords, password hashes, or storage credentials

### Requirement: Share Rate-Limit Filter Correlation
The share access and authenticated share-operation rate-limit filters SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every counter-dependency failure, verified-JTI attribute failure, and limit-rejection event directly owned by those filters SHALL use that context without changing rate-limit keys, windows, thresholds, authentication order, fail-open behavior, or HTTP responses.

#### Scenario: Share rate-limit filter handles a traced request
- **WHEN** a share rate-limit filter emits a dependency-failure, attribute-failure, or rejection event for a request whose trace attribute has been established
- **THEN** the event SHALL retain the same request ID, actual handling instance, and bounded operation as the HTTP completion event

#### Scenario: HTTP operation remains distinct from the internal rate bucket
- **WHEN** access, browse, or save uses the bounded `share` HTTP operation, or an exact share-download route uses the bounded `download` HTTP operation
- **THEN** the filter event SHALL retain that HTTP operation even when save consumes the internal download rate-limit bucket

#### Scenario: Share rate-limit filter observes rate identities
- **WHEN** a filter observes a normalized client IP, verified token JTI, share code, counter key, count, window, or dependency result
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Share rate-limit filter handles credentials or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies owner or visitor credentials
- **THEN** the event SHALL retain the bounded operation with explicit JSON null request correlation when needed and SHALL NOT contain raw Authorization or `X-Share-Token` values, share tokens, token JTIs, passwords, password hashes, or storage credentials

### Requirement: Typed Authentication Correlation
The system SHALL classify only the exact register, login, refresh, and logout paths as the bounded `auth` operation and SHALL propagate their correlation explicitly by value across the JWT authentication filter, authentication controller, direct authentication-DTO events, and authentication-service coroutine boundaries.

#### Scenario: Client registers, logs in, refreshes, or logs out
- **WHEN** a request enters `/api/auth/register`, `/api/auth/login`, `/api/auth/refresh`, or `/api/auth/logout`
- **THEN** its JWT-filter events, response, HTTP completion event at the configured level, controller events, direct authentication-DTO events, and authentication-service events SHALL retain the same request ID, actual handling instance, and `auth` operation

#### Scenario: Authentication request has no upload or durable-job ownership
- **WHEN** an authentication request records an account, user ID, login-attempt count, refresh-token identifier, or operation-log result
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those authentication-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Authentication credentials remain secret
- **WHEN** any authentication path succeeds or fails
- **THEN** application events and typed correlation fields SHALL NOT contain passwords, password hashes, Authorization header values, access tokens, or refresh tokens

#### Scenario: Authentication classification remains bounded
- **WHEN** an authentication path is not exactly one of the four supported paths, including the collection root, a trailing slash, an extra suffix, or an unknown action
- **THEN** the authentication classifier SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

### Requirement: Typed Share Correlation
The system SHALL classify only registered non-download share path shapes as the bounded `share` operation and SHALL propagate their correlation explicitly by value across JWT and share-token authentication filters, the share controller, direct share-DTO events, share-service helper coroutines, and share-audit boundary. Share download metadata and content routes SHALL retain the separate bounded `download` operation.

#### Scenario: Owner or visitor manages or consumes a share
- **WHEN** a request creates, lists, inspects, updates, cancels, accesses, browses, or saves a share through a registered non-download share path
- **THEN** its authentication-filter events, response, HTTP completion event at the configured level, controller events, direct share-DTO events, and share-service events SHALL retain the same request ID, actual handling instance, and `share` operation; any resulting share-audit details SHALL persist the same request ID and `share` operation

#### Scenario: Share download remains a download operation
- **WHEN** a request enters `/api/share/download/{share_id}/{file_id}` or `/api/share/download/{share_id}/{file_id}/info`
- **THEN** its existing controller, direct share-download DTO, service, response, integrity, and statistics events SHALL retain the same request ID, actual handling instance, and `download` operation rather than being reclassified as `share`; any resulting share-download audit details SHALL persist the same request ID and `download` operation

#### Scenario: Share request has no upload or durable-job ownership
- **WHEN** a share request records a share code, internal share ID, file, folder, user, permission, password-attempt count, view count, cache value, or audit result
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those share-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Share credentials remain secret
- **WHEN** any share path succeeds or fails
- **THEN** application and audit events SHALL NOT contain a raw Share Token, `X-Share-Token` or owner Authorization header value, share password, or password hash

#### Scenario: Share classification remains bounded
- **WHEN** a path does not match the share collection, one-segment detail, cancel compatibility, access, browse, save, or exact share-download route shapes, including a trailing slash or extra path segment
- **THEN** the share classifiers SHALL NOT absorb it, and the route SHALL retain the `other` operation classification

### Requirement: Typed Core Administration Correlation
The system SHALL classify the core administration routes owned by `AdminController` as the fixed low-cardinality `admin` operation and SHALL propagate their correlation explicitly by value across JWT and administrator-authorization filters, the controller, direct administration-DTO events, administration-service coroutines, and operation-log audit boundary. The exact manual-cleanup route SHALL retain its separate `cleanup` operation.

#### Scenario: Administrator uses a core administration route
- **WHEN** an authenticated administrator lists, inspects, or changes users, reads storage or system statistics, lists, inspects, or cancels shares, or queries operation logs through a route registered by `AdminController`
- **THEN** its authentication/authorization-filter events, response, HTTP completion event at the configured level, controller events, direct administration-DTO events, and administration-service events SHALL retain the same request ID, actual handling instance, and `admin` operation

#### Scenario: Core administration writes an audit row
- **WHEN** a core administration request records a successful user, storage, or share audit event
- **THEN** its JSON audit details SHALL persist the same request ID and `admin` operation, SHALL use the authenticated administrator as the operator, SHALL remain valid for target names and share codes containing JSON metacharacters, and SHALL use an action name no longer than the existing `operation_logs.action VARCHAR(32)` contract

#### Scenario: Administrator sets user available space
- **WHEN** an administrator successfully changes another user's available space
- **THEN** the audit action SHALL be `admin.user.available_space_set`, which fits the existing schema without requiring a database migration

#### Scenario: Core administration has no upload or durable-job ownership
- **WHEN** a core administration request records administrator, user, share, file, storage, filter, pagination, dependency, or audit identifiers
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those administration-domain values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Administration credentials remain secret
- **WHEN** a core administration request succeeds or fails
- **THEN** application and audit events SHALL NOT contain an Authorization header value, JWT, password, password hash, share token, or storage credential

#### Scenario: Administration subdomain boundaries remain distinct
- **WHEN** the manual expired-cleanup route or a storage-job or storage-recovery administration route is handled
- **THEN** manual cleanup SHALL retain `cleanup`, while storage-job and storage-recovery controllers SHALL remain outside this core `AdminController` propagation requirement and SHALL NOT infer missing correlation from threads, credentials, or domain identifiers

### Requirement: Typed Storage Job Administration Correlation
The system SHALL propagate storage-job administration request correlation explicitly by value across `StorageJobAdminController`, storage-job administration service coroutines, and the dead-letter replay audit boundary. These routes SHALL retain the existing bounded `admin` operation classification.

#### Scenario: Administrator lists storage jobs
- **WHEN** an authenticated administrator lists persistent storage jobs
- **THEN** the response, HTTP completion event at the configured level, controller events, and storage-job administration service errors SHALL retain the same request ID, actual handling instance, and `admin` operation, while job ID, upload ID, lease owner, and state version remain null

#### Scenario: Administrator inspects or replays one storage job
- **WHEN** an authenticated administrator supplies a valid positive persistent storage-job ID to the detail or replay route
- **THEN** controller and storage-job administration service events SHALL retain the same request ID, actual handling instance, and `admin` operation and SHALL use that parsed database row ID as `job_id`; upload ID, lease owner, and state version SHALL remain null

#### Scenario: Storage-job path validation fails
- **WHEN** a detail or replay path does not contain a valid positive storage-job ID
- **THEN** request-scoped validation and HTTP completion events SHALL keep the response request ID and `admin` operation while job ID remains null, and the system SHALL NOT derive a job ID from raw path text, dedupe keys, aggregate IDs, payloads, or messages

#### Scenario: Administrator commits a dead-letter replay
- **WHEN** a confirmed non-dry-run replay atomically resets a dead-letter job and writes its operation-log row
- **THEN** the replay event SHALL retain the request context and parsed persistent job ID, and the JSON audit details SHALL persist the same non-empty request ID and `admin` operation in the replay transaction without recording credentials or storage payload contents

#### Scenario: Upload diagnostics list related jobs
- **WHEN** upload diagnostics internally call `ListRelatedToUpload` without entering a storage-job administration route
- **THEN** that helper SHALL remain outside this request-bound propagation requirement and SHALL NOT infer a storage-job request ID or job ID from the upload ID, staging prefix, related rows, or error message

### Requirement: Typed Storage Recovery Administration Correlation
The system SHALL propagate storage-recovery administration request correlation explicitly by value across `StorageRecoveryAdminController`, storage-recovery service coroutines, and each transactional audit boundary. These routes SHALL retain the existing bounded `admin` operation classification.

#### Scenario: Administrator inspects or releases an upload lease
- **WHEN** an authenticated administrator submits a valid upload ID to the lease-release route
- **THEN** the response, HTTP completion event at the configured level, controller events, and service events SHALL retain the same request ID, actual handling instance, and `admin` operation; request-owned events after validation SHALL use the validated upload ID, and events after a successful database read or mutation SHALL add only the returned state version and lease owner

#### Scenario: Administrator inspects or rebuilds upload cleanup
- **WHEN** an authenticated administrator submits a valid upload ID to the cleanup-rebuild route
- **THEN** request-owned events after validation SHALL use that upload ID, events after loading the upload row SHALL add its returned state version, and events after observing or creating a persistent cleanup job SHALL add only its returned positive job ID

#### Scenario: Administrator inspects or enqueues reconciliation
- **WHEN** an authenticated administrator submits a valid scan ID and fixed reconciliation scope
- **THEN** request and inspection events SHALL keep upload ID, lease owner, state version, and job ID null until a persistent storage-job row is actually observed or created; a resulting event MAY add that row's positive job ID, while scan ID, scope, dedupe key, page size, and cursor values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Recovery input is only a caller assertion
- **WHEN** a recovery request supplies `expected_state_version`, `expected_lease_owner`, a confirmation ID, or a raw path value that has not passed validation
- **THEN** the system SHALL NOT use those values as observed state version, lease owner, upload ID, or job ID; unavailable typed fields SHALL remain null on validation and conflict events

#### Scenario: Administrator commits a recovery command
- **WHEN** lease release, cleanup rebuild, or reconciliation enqueue atomically commits its mutation and operation-log row
- **THEN** JSON audit details SHALL persist the same non-empty request ID and `admin` operation together with the command's existing bounded business details, while dry-run and conflict paths SHALL NOT write an audit row

#### Scenario: Recovery diagnostics remain secret
- **WHEN** a storage-recovery command succeeds or fails
- **THEN** application and audit events SHALL NOT contain Authorization header values, administrator JWTs, object-storage credentials, object keys or prefixes, arbitrary cursors, or storage-job payloads

### Requirement: Side-Effect-Free Shared DTO Validation
The shared `DtoBase` parsing helpers SHALL be deterministic validation utilities that report failures only through their existing `Result<T>` contracts. They SHALL NOT emit application or framework logs, infer request correlation, or retain caller input.

#### Scenario: Shared validation rejects input
- **WHEN** a `DtoBase` helper rejects an invalid JSON body, required or optional field, path identifier, query value, or identifier array
- **THEN** it SHALL return the existing error code and normalized error message without emitting a log event or changing the parsing result contract

#### Scenario: Request-bound DTO validation fails
- **WHEN** a request DTO receives an error from a shared helper
- **THEN** the DTO, controller, or HTTP completion boundary that already owns the explicit request context MAY record the failure, and `DtoBase` SHALL NOT create a duplicate event with null request correlation

#### Scenario: Invalid input contains arbitrary text
- **WHEN** a shared path or query parser receives an arbitrary malformed value
- **THEN** `DtoBase` SHALL NOT write that value, its field name, or its failure message to logs; the returned normalized `ErrorInfo` remains the only shared-layer output

#### Scenario: Validation runs outside HTTP handling
- **WHEN** a shared helper is used without an HTTP request context
- **THEN** it SHALL remain side-effect free and SHALL NOT reconstruct request, operation, upload, job, lease, or state-version correlation from fields, values, threads, or messages

### Requirement: Explicit Transaction Boundary Correlation
The shared `TransactionRunner` SHALL accept caller-owned `LogContext` explicitly by value and retain it for every transaction failure event without changing the existing callback-error, exception, rollback, or commit result mapping.

#### Scenario: Managed transaction fails
- **WHEN** a managed transaction encounters a database or standard exception, a failed commit callback, or a rollback exception
- **THEN** every event emitted by the runner SHALL retain the caller-supplied request, operation, upload, job, lease-owner, and state-version values exactly

#### Scenario: Caller commits a manually managed transaction
- **WHEN** a service passes an explicit context to the standalone commit helper and commit is rejected because the transaction has persistent outstanding owners
- **THEN** the rejection event SHALL retain that context without deriving correlation from the owner count, transaction object, SQL, or error message

#### Scenario: Transaction has no caller context
- **WHEN** a non-request caller uses the compatibility default without supplying a context
- **THEN** unavailable correlation fields SHALL remain null, and the runner SHALL NOT consult thread-local state or infer identifiers from callback arguments, exceptions, or transaction state

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

### Requirement: Explicit File DTO Correlation
Every file-domain request DTO that emits a parsing or validation event SHALL accept caller-owned `LogContext` explicitly by value and SHALL use only that supplied context for the event. This SHALL cover upload initialization and completion, file listing and search, owner download metadata and content, rename, move, copy, and soft-delete parsing without changing their existing request or validation contracts.

#### Scenario: HTTP file request enters a direct-logging DTO
- **WHEN** a file controller invokes one of those DTO parsers with the request context it established before parsing
- **THEN** every direct DTO event SHALL retain the same request ID, actual handling instance, and bounded route operation, and a DTO-specific validation failure SHALL NOT create an additional application event with null request correlation

#### Scenario: DTO parses domain values
- **WHEN** the DTO observes an upload ID, file ID, folder ID, filename, search keyword, or any other request value
- **THEN** it SHALL NOT derive or overwrite upload ID, job ID, lease owner, or state version from that value; upload completion SHALL continue to add its validated upload ID at the controller boundary after successful parsing

#### Scenario: File DTO has no caller context
- **WHEN** a unit, utility, or compatibility caller invokes a parser without providing `LogContext`
- **THEN** parsing and validation behavior SHALL remain unchanged and any direct DTO event SHALL use explicit JSON null correlation rather than thread-local or request-value inference

### Requirement: Typed File Query Correlation
The system SHALL classify file-list, numeric file-detail, and file-search requests as the bounded `file_query` operation and SHALL propagate their correlation explicitly by value across controller, direct file-DTO events, and query-service coroutine boundaries.

#### Scenario: User lists, inspects, or searches files
- **WHEN** an authenticated request enters a file-list, numeric file-detail, or file-search route
- **THEN** its response, HTTP completion event, controller events, direct file-DTO events, and query-service events SHALL retain the same request ID, actual handling instance, and `file_query` operation

#### Scenario: File query has no upload or durable-job ownership
- **WHEN** a file query records identifiers such as a file ID, parent folder ID, or search keyword
- **THEN** upload ID, job ID, lease owner, and state version SHALL remain null, and those query values SHALL NOT be overloaded into typed correlation fields

#### Scenario: Another file-domain route is classified
- **WHEN** the request targets upload, download, or a file-mutation route
- **THEN** the file-query classifier SHALL NOT absorb it, and the route SHALL retain its existing bounded operation classification

### Requirement: Typed File Mutation Correlation
The system SHALL classify numeric file rename, drive move, drive copy, and file soft-delete requests as the bounded `file_mutation` operation and SHALL propagate their correlation explicitly by value across controller, direct file-DTO events, mutation-service, and move-to-trash coroutine boundaries.

#### Scenario: User renames, moves, copies, or soft-deletes drive items
- **WHEN** an authenticated request enters a numeric rename, move, copy, or supported soft-delete route
- **THEN** its response, HTTP completion event, controller events, direct file-DTO events, mutation-service events, and delete subflow events SHALL retain the same request ID, actual handling instance, and `file_mutation` operation

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
