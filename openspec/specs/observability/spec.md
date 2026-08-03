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

#### Scenario: System storage aggregation fails
- **WHEN** the system-information storage aggregation encounters a database exception
- **THEN** its directly owned event SHALL use the fixed complete `Failed to get storage stats` summary and SHALL NOT append exception text, SQL, connection details, credentials, or tokens
- **AND** the response SHALL retain any collected or default storage values and the existing successful system-information contract

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

#### Scenario: User-profile service dependency work fails
- **WHEN** profile read or update, password change, or storage-statistics database or standard processing fails
- **THEN** the directly owned event SHALL use its fixed complete failure summary and SHALL NOT append exception text, SQL, connection details, credentials, or tokens
- **AND** password change and profile update SHALL map Drogon `UnexpectedRows` structurally to the existing user-not-found error without searching exception text, while all other domain errors and public responses SHALL remain unchanged

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

### Requirement: Token Service Correlation
The shared token service SHALL accept explicit correlation by value for request-reachable access-token, refresh-token, and share-token generation, verification, rotation, revocation parsing, and revoked-token rejection events. Authentication services and JWT/share authentication filters SHALL pass their already established request context without changing token claims, signatures, TTLs, Redis keys, CAS behavior, revocation behavior, error mapping, or public responses.

#### Scenario: Token service handles an authenticated request
- **WHEN** access or refresh token work is initiated by login, refresh, logout, or JWT authentication
- **THEN** every directly owned request event SHALL retain the caller's request ID, actual handling instance, and bounded HTTP operation across the authentication CPU-pool boundary when applicable

#### Scenario: Token service handles a share request
- **WHEN** share access generates a token or share authentication verifies or rejects a token
- **THEN** every directly owned request event SHALL retain the caller's request ID, actual handling instance, and bounded `share` or `download` operation supplied by the calling route

#### Scenario: Token service observes token-domain values
- **WHEN** the service observes a user ID, username, token JTI, share code, internal share ID, permission, Redis result, duration, or parsing result
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain as supplied by the caller

#### Scenario: Token service handles credentials or a context-free caller
- **WHEN** token work receives a JWT secret, access token, refresh token, share token, Authorization value, or `X-Share-Token` value, or a unit/utility caller omits explicit correlation
- **THEN** application events SHALL NOT contain those credential values, and the compatibility call SHALL use explicit JSON null correlation rather than thread-local or token-derived state

#### Scenario: Token service records a token failure
- **WHEN** access-token, refresh-token, or share-token verification or parsing fails, revocation JTI extraction throws, or share-token generation throws
- **THEN** the directly owned event SHALL use its fixed complete failure summary and SHALL NOT append exception text, provider text, claims, token values, signing secrets, endpoints, or connection details

#### Scenario: Token service classifies token expiry
- **WHEN** JWT verification throws a token verification exception
- **THEN** expiry SHALL be identified only by comparing the structured error code with `token_verification_error::token_expired`, and SHALL NOT be inferred by searching exception text

#### Scenario: Token service emits process events
- **WHEN** token-singleton initialization, authentication CPU-pool creation, cache-maintenance timer startup, metrics timer startup, periodic pool metrics, or cache eviction emits a process event
- **THEN** the event SHALL use fixed `operation=auth_runtime`, SHALL accept only the structured logger's registered process instance, and SHALL keep request ID, upload ID, job ID, lease owner, and state version JSON null because no HTTP request or durable job owns that event

#### Scenario: Token process event records bounded diagnostics
- **WHEN** a token process event records CPU-pool threads, a maintenance or metrics interval, periodic submitted/completed/active/peak counts, or cache-eviction counts and sizes
- **THEN** its message SHALL contain only those bounded diagnostics and SHALL NOT contain request context, a manually copied instance ID, token, JTI, token hash, cache key, credential, secret, endpoint, or exception text; token, pool, timer, cache, metric-reset, and log-level behavior SHALL remain unchanged

### Requirement: Redis Service Correlation
The shared Redis service SHALL accept explicit correlation by value for every command API and SHALL apply the supplied context to every directly owned command success, protocol, parsing, and dependency-failure event. Authentication, token, share, file-list cache, rate-limit, administrator status, and readiness callers SHALL pass their already established context without changing Redis commands, keys, values, TTLs, transactions, Lua scripts, CAS behavior, metrics, error codes, HTTP status, or dependency-failure policy.

#### Scenario: A request reaches Redis through a shared service
- **WHEN** an authenticated, share, file, administrator, or readiness request performs Redis work through a service or cache helper
- **THEN** every directly owned Redis event SHALL retain the caller's request ID, actual handling instance, and bounded HTTP operation across all helper and coroutine boundaries

#### Scenario: A request reaches Redis through a rate-limit filter
- **WHEN** any owner, visitor, upload, download, folder, administrator, registration, or trash fixed-window filter increments its Redis counter
- **THEN** the counter adapter and Redis event SHALL receive the same request context used by the filter without changing its key, window, threshold, authentication order, or fail-open behavior

#### Scenario: Redis observes command inputs and dependency failures
- **WHEN** the service observes a key, value, expected or replacement value, Lua script, TTL, user or token identifier, Redis exception, or parse failure
- **THEN** its application event SHALL contain only the fixed command name and bounded result metadata, SHALL NOT contain the key, value, script, exception text, credential, token hash, JTI, share code, IP address, file-list payload, or connection detail, and SHALL NOT derive upload ID, job ID, lease owner, or state version from any command input

#### Scenario: Redis returns a domain error
- **WHEN** a Redis exception, result parse failure, or missing key is converted to `ErrorInfo`
- **THEN** the result SHALL use only the corresponding default business message and SHALL NOT contain the exception text, endpoint, connection detail, key, value, command parameter, or credential

#### Scenario: Redis receives no request context
- **WHEN** a unit, utility, background, or compatibility caller omits explicit correlation
- **THEN** the command event SHALL use explicit JSON null request and ownership correlation rather than thread-local, key-derived, or value-derived state

#### Scenario: Redis service initializes
- **WHEN** the singleton accepts its Redis client before serving command work
- **THEN** the initialization event SHALL remain a context-free process event and SHALL NOT disclose endpoint or credential data

### Requirement: Quota Service Correlation
The shared quota service SHALL accept caller-owned `LogContext` explicitly by value for all standalone and transaction-client reservation, release, commit, consumption, adjustment, and reconciliation entry points. Upload lifecycle, file mutation, and trash deletion callers SHALL pass their already established context without changing PostgreSQL statements, conditional-update invariants, zero-byte behavior, `Result` values, quota accounting, transaction ownership, or public responses.

#### Scenario: A request performs quota accounting
- **WHEN** an upload, file mutation, trash deletion, cleanup, or durable Worker flow reaches the quota service through an existing request or job boundary
- **THEN** every directly owned quota event SHALL retain the supplied request, actual instance, bounded operation, upload ID, job ID, lease owner, and state version exactly as provided across standalone and transaction-client overloads

#### Scenario: Quota accounting observes domain and dependency values
- **WHEN** the service observes a user ID, byte count, signed adjustment, affected-row count, domain error, SQL failure, or database exception
- **THEN** it SHALL NOT infer or overwrite any typed correlation field, and its application messages SHALL use fixed event names without those values, SQL, connection details, or exception text

#### Scenario: A quota caller omits correlation
- **WHEN** a unit, utility, migration, or compatibility caller invokes any quota entry point without `LogContext`
- **THEN** request and ownership fields SHALL remain JSON null rather than being reconstructed from parameters, database rows, thread-local state, or messages

#### Scenario: Quota service initializes
- **WHEN** a quota service instance is constructed without request-owned work
- **THEN** its initialization event SHALL remain a context-free process event while all operation events SHALL use the explicit per-call context

### Requirement: Shared File Persistence Correlation
The folder-location repository and shared file persistence helpers SHALL accept caller-owned `LogContext` explicitly by value for folder-location resolution, trash-record insertion, active-file deletion, and active-folder deletion. Upload lifecycle, file mutation, and trash service callers SHALL pass their already established context through the utility and service forwarding boundaries without changing PostgreSQL statements, transaction ownership, affected-row interpretation, rollback behavior, domain results, or public responses.

#### Scenario: A request reaches a shared file persistence failure
- **WHEN** a folder lookup, trash insertion, file deletion, or folder deletion encounters a PostgreSQL exception
- **THEN** the directly owned event SHALL retain the supplied request ID, actual instance, bounded operation, upload ID, job ID, lease owner, and state version exactly as provided across repository, utility, and service boundaries

#### Scenario: A shared persistence helper observes domain and dependency values
- **WHEN** the helper observes a user, folder, file, item name, path, batch, SQL statement, affected-row count, or database exception
- **THEN** its event SHALL use a fixed operation-specific message without those values, SQL, connection details, or exception text, and SHALL NOT infer or overwrite any typed correlation field

#### Scenario: A shared persistence caller omits correlation
- **WHEN** a unit, utility, migration, or compatibility caller invokes one of the shared entry points without `LogContext`
- **THEN** request and ownership fields SHALL remain JSON null rather than being reconstructed from parameters, database state, thread-local state, or messages

#### Scenario: A move-to-trash transaction fails in a shared helper
- **WHEN** trash insertion, active-file deletion, or active-folder deletion fails inside the existing move-to-trash transaction
- **THEN** the transaction SHALL preserve its existing rollback and stable public error semantics while the helper event remains correlated and redacted

### Requirement: Shared Content Lifecycle Correlation
The shared content service SHALL accept caller-owned `LogContext` explicitly by value for standalone and transaction-client MD5 lookup, existing-ID lookup, reference acquisition, single and batch reference increments, reference decrement with Blob-GC enqueue, and the internal Blob-GC reference gate. Upload lifecycle, file mutation, sharing, and trash callers SHALL pass their already established context without changing PostgreSQL statements, row locking, reference-count invariants, garbage-collection admission, transaction ownership, `Result` or optional values, or public responses.

#### Scenario: A request or durable job reaches content lifecycle persistence
- **WHEN** an upload, copy, shared-item save, trash deletion, cleanup request, or claimed expiration job enters the content service
- **THEN** every directly owned operation event SHALL retain the supplied request ID, actual instance, bounded operation, upload ID, job ID, lease owner, and state version exactly as provided across public overloads and the internal reference gate

#### Scenario: Content lifecycle observes domain and dependency values
- **WHEN** the service observes a content ID, hash, size, path, MIME type, delta, batch, affected-row count, domain error, SQL failure, database exception, or garbage-collection payload
- **THEN** its application event SHALL use a fixed operation-specific message without those values, SQL, connection details, exception text, authorization material, or storage credentials, and SHALL NOT infer or overwrite any typed correlation field

#### Scenario: A content caller omits correlation
- **WHEN** a unit, utility, migration, or compatibility caller invokes a public content entry point without `LogContext`
- **THEN** request and ownership fields SHALL remain JSON null rather than being reconstructed from content metadata, database state, thread-local state, errors, or messages

#### Scenario: Content service initializes
- **WHEN** a content service instance is constructed without request-owned work
- **THEN** its initialization event SHALL remain a context-free process event while operation events use only the explicit per-call context

#### Scenario: Content persistence rejects or fails a reference change
- **WHEN** reference acquisition, increment, decrement, affected-row verification, or the Blob-GC gate rejects or fails inside an existing transaction
- **THEN** the service SHALL preserve its current rollback, conflict/internal-error, optional/set, and enqueue semantics while its directly owned event remains correlated and redacted

### Requirement: Upload Rate-Limit Filter Correlation
The route-owned upload rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every request-attribute failure, counter-dependency failure, successful check, and limit-rejection event directly owned by that filter SHALL use the context without changing its init, chunk, complete, and cancel route ownership, user fixed-window key, configured window or threshold, fail-open behavior, or HTTP response.

#### Scenario: Upload rate-limit filter handles traced upload routes
- **WHEN** the filter emits an event for an upload init, chunk, complete, or cancel request whose trace attribute has been established
- **THEN** the event SHALL retain the same request ID and actual handling instance as the HTTP completion event, and SHALL retain the route's bounded `upload_init`, `upload_chunk`, `upload_complete`, or `upload_cancel` operation instead of collapsing all routes into one rate-limit operation

#### Scenario: Upload rate-limit filter observes identity and counter values
- **WHEN** the filter observes a user ID, path, counter key, count, window, threshold, or dependency result before the upload controller validates a business upload ID
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Upload rate-limit filter handles credentials, content, or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies an Authorization value, upload parameters, metadata, or chunk content
- **THEN** the event SHALL retain the bounded route operation with explicit JSON null request correlation when needed and SHALL NOT contain the Authorization value, access token, password, upload request body, file content, or storage credential

### Requirement: Owner Download Rate-Limit Filter Correlation
The route-owned owner-download rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every counter-dependency failure, successful check, and limit-rejection event directly owned by that filter SHALL use the context without changing its download-info and download-content route ownership, user fixed-window key, configured window or threshold, fail-open behavior, HTTP response, or download metadata side effects.

#### Scenario: Owner download rate-limit filter handles traced routes
- **WHEN** the filter emits an event for an owner download-info or download-content request whose trace attribute has been established
- **THEN** the event SHALL retain the same request ID, actual handling instance, and bounded `download` operation as the HTTP completion event

#### Scenario: Owner download rate-limit filter observes identity and counter values
- **WHEN** the filter observes a user ID, path, file path parameter, counter key, count, window, threshold, or dependency result before the file controller validates a business file ID
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Owner download rate-limit filter handles credentials or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies an Authorization value, access token, Range header, or download parameters
- **THEN** the event SHALL retain the bounded `download` operation with explicit JSON null request correlation when needed and SHALL NOT contain the Authorization value, access token, Range value, password, file content, or storage credential

### Requirement: Folder Rate-Limit Filter Correlation
The route-owned folder rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every counter-dependency failure, successful check, and limit-rejection event directly owned by that filter SHALL use the context without changing its create, tree, breadcrumb, and rename route ownership, user fixed-window key, configured window or threshold, fail-open behavior, HTTP response, or folder mutation side effects.

#### Scenario: Folder rate-limit filter handles traced query routes
- **WHEN** the filter emits an event for tree or breadcrumb
- **THEN** it SHALL retain the same request ID and actual handling instance as the HTTP completion event with bounded `folder_query`

#### Scenario: Folder rate-limit filter handles traced mutation routes
- **WHEN** the filter emits an event for create or rename
- **THEN** it SHALL retain the same request ID and actual handling instance as the HTTP completion event with bounded `folder_mutation`

#### Scenario: Folder rate-limit filter observes identity and counter values
- **WHEN** the filter observes a user ID, path, folder path parameter, counter key, count, window, threshold, request field, or dependency result before the folder controller validates the business request
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Folder rate-limit filter handles credentials, content, or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies an Authorization value, access token, folder name, parent ID, rename body, or other request content
- **THEN** the event SHALL retain the bounded route operation with explicit JSON null request correlation when needed and SHALL NOT contain the Authorization value, access token, password, request body, folder name, or storage credential

### Requirement: Admin Rate-Limit Filter Correlation
The route-owned administrator rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every counter-dependency failure, successful check, and limit-rejection event directly owned by that filter SHALL use the context without changing administrator authentication order, route ownership, user fixed-window key, configured window or threshold, fail-open behavior, HTTP response, or administration side effects.

#### Scenario: Admin rate-limit filter handles traced administration routes
- **WHEN** the filter emits an event for an administrator user, share, statistics, log, upload-diagnostic, storage-job, or recovery route
- **THEN** it SHALL retain the same request ID and actual handling instance as the HTTP completion event with bounded `admin`

#### Scenario: Admin rate-limit filter handles traced cleanup route
- **WHEN** the filter emits an event for the exact `/api/admin/maintenance/cleanup/expired` route
- **THEN** it SHALL retain the same request ID and actual handling instance as the HTTP completion event with bounded `cleanup`, even though the route shares the administrator user counter

#### Scenario: Admin rate-limit filter observes identity and route identifiers
- **WHEN** the filter observes a user ID, path, upload ID, storage-job ID, scan ID, counter key, count, window, threshold, request field, or dependency result before the administrator controller validates the business request
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Admin rate-limit filter handles credentials, content, or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies an Authorization value, access token, confirmation, reason, or other request content
- **THEN** the event SHALL retain the bounded route operation with explicit JSON null request correlation when needed and SHALL NOT contain the Authorization value, access token, password, request body, recovery reason, or storage credential

### Requirement: Trash User Rate-Limit Filter Correlation
The route-owned generic user rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every attribute failure, counter-dependency failure, successful check, limit rejection, and duration event directly owned by that filter SHALL use the context without changing its five trash route declarations, `rate:api` user fixed-window key, fixed 100-request threshold, 60-second window, Redis initialization, fail-open behavior, HTTP response, or trash side effects.

#### Scenario: Trash user rate-limit filter handles traced routes
- **WHEN** the filter emits an event for list, restore, either permanent-delete route, or delete-all
- **THEN** it SHALL retain the same request ID and actual handling instance as the HTTP completion event with bounded `trash`

#### Scenario: Trash user rate-limit filter rejects a destructive request
- **WHEN** delete-all exceeds the current user fixed-window threshold
- **THEN** the warning and failure-duration events SHALL retain the response request and instance correlation, the existing 429/`10005` envelope and three `X-RateLimit-*` headers SHALL remain unchanged, `Retry-After` SHALL remain absent, and no trash item SHALL be deleted

#### Scenario: Trash user rate-limit filter observes identity and request values
- **WHEN** the filter observes a user ID, trash ID, counter key, count, window, threshold, request field, duration, or dependency result before the trash controller validates the business request
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Trash user rate-limit filter handles credentials, content, or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a request supplies an Authorization value, access token, trash ID, delete body, or other request content
- **THEN** the event SHALL retain bounded `trash` with explicit JSON null request correlation when needed and SHALL NOT contain the Authorization value, access token, password, request body, trash ID, file content, or storage credential

### Requirement: Register Rate-Limit Filter Correlation
The register rate-limit filter SHALL build explicit request correlation at filter entry from the request trace attribute and the existing bounded HTTP operation classifier. Every counter-dependency failure, successful check, and limit-rejection event directly owned by that filter SHALL use the context without changing the exact-path scope, normalized-IP key, configured window or threshold, fail-open behavior, or HTTP response.

#### Scenario: Register rate-limit filter handles a traced request
- **WHEN** the filter emits a dependency-failure, successful-check, or rejection event for an exact `/api/auth/register` request whose trace attribute has been established
- **THEN** the event SHALL retain the same request ID, actual handling instance, and bounded `auth` operation as the HTTP completion event

#### Scenario: Register rate-limit filter observes client and counter values
- **WHEN** the filter observes a normalized client IP, counter key, count, window, threshold, or dependency result
- **THEN** it SHALL NOT derive upload ID, job ID, lease owner, or state version from that value, and those fields SHALL remain null

#### Scenario: Register rate-limit filter handles registration credentials or missing trace state
- **WHEN** a direct caller omits the request trace attribute or a registration request contains account, email, or password fields
- **THEN** the event SHALL retain the bounded `auth` operation with explicit JSON null request correlation when needed and SHALL NOT contain the registration body, password, password hash, Authorization value, token, or storage credential

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

#### Scenario: Authentication service dependency work fails
- **WHEN** authentication-service database work or standard processing throws, or login rate-limit maintenance receives a Redis domain error
- **THEN** the directly owned event SHALL use its fixed complete failure summary and SHALL NOT append exception text, downstream domain messages, SQL, connection details, Redis keys or values, credentials, or tokens
- **AND** username and email conflict handling, database-time account locking, login rate-limit fail-open behavior, token rotation and revocation, logout audit fail-open behavior, domain errors, and public responses SHALL remain unchanged

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

#### Scenario: Share audit persistence fails
- **WHEN** a share or download request encounters a database or standard exception while persisting its audit event
- **THEN** the directly owned event SHALL use only the fixed complete `Failed to record share audit event` summary with the caller's existing typed correlation and SHALL NOT append action, internal share ID, share code, exception text, SQL, connection details, credentials, or tokens
- **AND** audit persistence SHALL remain fail-open without automatic retry, while successful audit content, business results, and public responses SHALL remain unchanged

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

#### Scenario: Storage-job list or detail processing fails
- **WHEN** the storage-job administration service catches a database, row-decoding, or other standard exception while listing jobs or reading one job
- **THEN** the service SHALL log only the fixed complete `Storage job admin list failed` or `Storage job admin detail failed` event and SHALL NOT append exception text, SQL, connection details, job IDs, payloads, object locators, credentials, or tokens to the message
- **AND** detail correlation SHALL continue to carry the validated persistent job ID only in the typed `job_id` field, while error codes and public messages remain unchanged

#### Scenario: Storage-job path validation fails
- **WHEN** a detail or replay path does not contain a valid positive storage-job ID
- **THEN** request-scoped validation and HTTP completion events SHALL keep the response request ID and `admin` operation while job ID remains null, and the system SHALL NOT derive a job ID from raw path text, dedupe keys, aggregate IDs, payloads, or messages

#### Scenario: Administrator commits a dead-letter replay
- **WHEN** a confirmed non-dry-run replay atomically resets a dead-letter job and writes its operation-log row
- **THEN** the replay event SHALL retain the request context and parsed persistent job ID, and the JSON audit details SHALL persist the same non-empty request ID and `admin` operation in the replay transaction without recording credentials or storage payload contents

#### Scenario: Upload diagnostics list related jobs
- **WHEN** upload diagnostics pass their caller-owned context to `ListRelatedToUpload` without entering a storage-job administration route
- **THEN** that helper SHALL preserve the supplied request, instance, `admin`, upload, state-version, and lease-owner fields while job ID remains null, and SHALL NOT infer or overwrite correlation from the upload ID, staging prefix, related rows, or error message

### Requirement: Typed Upload Diagnostic Correlation
The system SHALL propagate administrator upload-diagnostic correlation explicitly by value across `UploadDiagnosticController`, `UploadDiagnosticService`, staging-object HEAD calls, and the related-storage-job query without changing authentication, validation, database reads, object inspection, pagination, response data, or the read-only contract. These routes SHALL retain the existing bounded `admin` operation classification.

#### Scenario: Administrator validates an upload diagnostic request
- **WHEN** an authenticated administrator calls `/api/admin/uploads/{upload_id}/diagnostics`
- **THEN** the controller SHALL create `admin` context before DTO parsing, SHALL keep upload ID null for rejected raw path values, and SHALL add only the DTO-validated non-empty upload ID before emitting or delegating request-owned events

#### Scenario: Upload diagnostics load authoritative task ownership
- **WHEN** the diagnostic service loads the PostgreSQL upload-task row for the same caller-owned upload ID
- **THEN** it SHALL add only that row's state version and optional lease owner, SHALL preserve request ID, actual instance, `admin`, and upload ID through every staging HEAD and related-job query, and SHALL keep job ID null because the response can contain zero or multiple jobs

#### Scenario: A diagnostic caller omits correlation
- **WHEN** a unit, utility, or compatibility caller invokes the diagnostic service or related-job helper without `LogContext`
- **THEN** request and ownership fields SHALL remain JSON null, and neither service SHALL infer them from request values, task rows, staging prefixes, object descriptors, related jobs, thread-local state, or messages

#### Scenario: Upload diagnostic messages remain bounded and secret-free
- **WHEN** diagnostics succeed or observe validation, PostgreSQL, parsing, staging, or related-job failures
- **THEN** application messages SHALL use fixed event names and SHALL NOT contain raw path or pagination values, user or file metadata, hashes, object keys or prefixes, ETags, job payload or errors, SQL, connection details, exception text, Authorization values, JWTs, or storage credentials

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

#### Scenario: Storage-recovery inspection throws
- **WHEN** lease dry-run inspection, cleanup-rebuild dry-run inspection, or reconciliation enqueue inspection catches a standard exception
- **THEN** the service SHALL log only the fixed complete `Upload lease release dry-run failed`, `Upload cleanup rebuild dry-run failed`, or `Storage reconciliation enqueue inspection failed` event and SHALL NOT append upload ID, scan ID, exception text, SQL, connection details, object locators, credentials, or tokens to the message
- **AND** the validated upload ID SHALL remain only in typed context where applicable, while the three existing `InternalError` public messages, transactions, audits, and command behavior remain unchanged

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

#### Scenario: Managed transaction catches an exception
- **WHEN** the runner catches a database or standard exception, including an exception raised while rolling back
- **THEN** database and standard transaction failures SHALL log only the fixed complete `Database transaction failed` event, while rollback failures SHALL log only `Transaction rollback failed`
- **AND** neither event SHALL contain exception text, SQL, connection details, transaction objects, domain identifiers, paths, credentials, or tokens, while callback errors, default error mapping, rollback precedence, commit waiting, and existing fixed commit diagnostics remain unchanged

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

### Requirement: Typed Operation Log Query Correlation
The exact operation-log list route SHALL use the bounded `operation_log` HTTP operation, and its controller and shared service SHALL accept and propagate caller-owned `LogContext` explicitly by value without changing pagination, database queries, stored audit data, error results, or the public response contract.

#### Scenario: An authenticated user lists operation logs
- **WHEN** an authenticated request reaches exact path `/api/logs`
- **THEN** the HTTP completion event, controller events, and any service failure event SHALL retain the same request ID, actual handling instance, and `operation_log` operation, while upload ID, job ID, lease owner, and state version remain null

#### Scenario: Operation-log route classification stays bounded
- **WHEN** the HTTP classifier observes `/api/logs`, a trailing slash, an additional path segment, or another unregistered logs path
- **THEN** only exact `/api/logs` SHALL map to `operation_log`, and every other shape SHALL remain `other`

#### Scenario: The operation-log boundary observes sensitive diagnostics
- **WHEN** the controller or service observes a peer address, authenticated user, pagination value, audit entry, database exception, or error detail
- **THEN** its application event SHALL use a fixed message without those values, SHALL NOT derive typed ownership fields from them, and SHALL NOT expose authorization values, stored audit details, user agents, IP addresses, SQL, connection details, or exception text

#### Scenario: An operation-log service caller omits correlation
- **WHEN** a unit, utility, or compatibility caller invokes the query or write entry point without a context
- **THEN** request and ownership correlation SHALL remain JSON null, the service SHALL NOT consult thread-local state or infer correlation from entry/query values, and constructor initialization SHALL remain a context-free process event

### Requirement: Request Trace Visibility
The system SHALL associate requests with trace identifiers for log correlation and response visibility. When a safe caller-provided identifier is unavailable, the server SHALL generate a lowercase UUID v4 from the process-initialized cryptographic system random source without a process-local pseudorandom generator.

#### Scenario: Request is handled
- **WHEN** the system handles an HTTP request
- **THEN** it SHALL make the request trace identifier available for logging and response propagation

#### Scenario: Server generates request identifiers concurrently
- **WHEN** requests without safe caller-provided trace identifiers are resolved concurrently across event-loop threads or independent instances
- **THEN** every generated identifier SHALL retain the lowercase UUID v4 version and variant layout, SHALL draw its random bytes from the cryptographic system random source, and SHALL NOT depend on `std::random_device`, `std::mt19937`, or another process-local pseudorandom engine

### Requirement: Typed Metrics Snapshot Failure Correlation
The exact `/metrics` scrape endpoint SHALL establish the bounded `metrics` operation before querying PostgreSQL and SHALL pass that request correlation explicitly by value to the database-snapshot failure boundary without changing the Prometheus response contract.

#### Scenario: Metrics snapshot query fails
- **WHEN** a traced `/metrics` request cannot query its PostgreSQL-backed snapshot
- **THEN** the response and the single directly owned failure event SHALL retain the same request ID and actual handling instance, the event SHALL use `operation=metrics`, and upload ID, job ID, lease owner, and state version SHALL remain null

#### Scenario: Metrics snapshot failure degrades safely
- **WHEN** the PostgreSQL-backed portion of a scrape is unavailable
- **THEN** the endpoint SHALL still return HTTP 200 Prometheus text with process-local metrics and `disk_metrics_snapshot_success 0`

#### Scenario: Metrics snapshot failure is recorded
- **WHEN** the service catches a snapshot query failure
- **THEN** it SHALL emit one fixed `WARN` application message without SQL, exception text, endpoint, connection detail, credentials, or identifiers embedded in the message, and it SHALL NOT infer typed ownership fields from runtime or database values

#### Scenario: Metrics rendering has no HTTP caller
- **WHEN** a unit, utility, or compatibility caller renders metrics without supplying correlation
- **THEN** request and ownership fields SHALL remain JSON null rather than being reconstructed from thread-local state, the runtime instance, query results, or failure text

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

#### Scenario: File query database access throws
- **WHEN** file-list lookup, download-metadata update, or file search catches a database exception
- **THEN** the service SHALL log only the fixed complete `File list query failed`, `File download metadata update failed`, or `File search failed` event and SHALL NOT append file, user, or folder ID, search terms, exception text, SQL, connection details, paths, credentials, or tokens to the message
- **AND** caller correlation, the existing file-list `InternalError` public message, best-effort metadata-update behavior, default search response, queries, cache behavior, pagination, and responses SHALL remain unchanged

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

#### Scenario: File soft-delete service throws unexpectedly
- **WHEN** `FileController` catches a standard exception from the soft-delete service boundary
- **THEN** its directly owned event SHALL use only the fixed complete `Unexpected exception in delete file` summary with the request's existing typed correlation and SHALL NOT append exception text, user, file, or folder identifiers, SQL, connection details, paths, credentials, or tokens
- **AND** the controller SHALL retain the existing `InternalError` and `Internal error during file deletion` public response without changing parsing, service delegation, trash transactions, rollback, or success behavior

#### Scenario: File copy catches a database exception
- **WHEN** explicit-file fetch, file-name conflict lookup, file-content lookup, folder-name conflict lookup, folder-content lookup, or copied-file insertion catches a database exception
- **THEN** the directly owned event SHALL use only its operation-specific fixed complete summary with the request's existing typed correlation and SHALL NOT append exception text, SQL, connection details, file, folder, content, or user identifiers, names, paths, quota values, credentials, or tokens
- **AND** existing chunk or folder skipping, reservation release when already reserved, batch rollback, partial-success response, reference-count, item-count, quota, cache, and retry behavior SHALL remain unchanged

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

#### Scenario: Folder query database access throws
- **WHEN** folder-tree, parent-ownership, or breadcrumb lookup catches a database exception
- **THEN** the service SHALL log only the fixed complete `Folder tree query failed`, `Parent folder ownership lookup failed`, or `Breadcrumb query failed` event and SHALL NOT append folder, parent, or user ID, exception text, SQL, connection details, paths, credentials, or tokens to the message
- **AND** caller correlation, the existing folder-tree and breadcrumb `InternalError` public messages, parent-ownership `FolderNotFound` mapping, queries, tree construction, cache behavior, and responses SHALL remain unchanged

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

#### Scenario: Completion helper dependency fails
- **WHEN** finalize lease renewal, finalize-error recording, staging-mismatch finding persistence, or reconciliation-job enqueue throws, or reconciliation-job construction returns an error
- **THEN** the directly owned event SHALL use only its operation-specific fixed complete summary and existing typed correlation, and SHALL NOT append upload, lease, or version values, exception or domain-error text, SQL, connection details, object locators, paths, credentials, or tokens to the message
- **AND** existing renewal error mapping, best-effort recording and persistence, construction early return, finding details, job payload and dedupe, compare-and-set, response, and retry behavior SHALL remain unchanged

#### Scenario: Completion read dependency throws
- **WHEN** file-name precheck, finalize-lease claim, completed-result replay, staging-session load, assembly-chunk descriptor load, or finalize-metadata lookup catches an exception
- **THEN** the directly owned event SHALL use only its operation-specific fixed complete summary and existing typed correlation, and SHALL NOT append upload, user, file, or folder identifiers, lease or version values, exception text, SQL, connection details, object locators, paths, credentials, or tokens to the message
- **AND** existing precheck and metadata fallback values, claim and replay error mapping, finalize-error recording, timing, compare-and-set, assembly, deduplication, conflict, response, and retry behavior SHALL remain unchanged

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

#### Scenario: Worker observes a dependency or handler failure
- **WHEN** a claimed job receives a downstream domain failure or the Worker catches a rollback, lease, handler, result-persistence, or claim exception
- **THEN** the newly persisted error SHALL use a fixed task or operation summary, SHALL preserve error-code-based retry classification, and SHALL NOT contain the downstream message, unknown job type, exception text, SQL, connection detail, endpoint, object locator, payload value, or credential
- **AND** each exception catch-path event SHALL use its fixed failure summary and existing typed correlation and SHALL NOT contain the downstream message or exception text

#### Scenario: Reconciliation page processing fails
- **WHEN** `StorageReconciliationService` catches a database or standard exception while processing a validated reconciliation page
- **THEN** it SHALL log only the fixed complete `Storage reconciliation database failure` or `Storage reconciliation failed` event and SHALL NOT append scope, scan ID, exception text, SQL, connection details, object locators, credentials, or tokens to the message
- **AND** the caller-owned durable-job context, existing `InternalError` mapping, pagination, finding persistence, repair enqueueing, and retry behavior SHALL remain unchanged

#### Scenario: Worker correlates staging cleanup to an upload
- **WHEN** a claimed `staging_cleanup` job has a valid cleanup payload whose non-empty upload ID exactly matches its aggregate ID
- **THEN** job-level Worker events SHALL carry that value as the upload ID, while malformed staging jobs and every other job type SHALL keep the job-level upload ID null; an expiration lifecycle event MAY add an upload ID and state version only from the actual successful upload-task transition

#### Scenario: Worker no longer owns the completed attempt
- **WHEN** the Worker persists a succeeded, retry, or dead-letter result, or can no longer confirm continued ownership
- **THEN** the execution-completed event SHALL retain the persistent job ID and instance but SHALL use a null lease owner

#### Scenario: Worker runtime has no claimed job
- **WHEN** a Worker runtime starts, drains, or reports an aggregate poll result outside an individual claimed-job boundary
- **THEN** the event SHALL use the actual process instance, SHALL use `storage_worker_runtime` for lifecycle events or `storage_worker_poll` for poll events, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: Worker runtime poll fails
- **WHEN** the Worker callback returns a domain failure or throws an exception
- **THEN** the runtime SHALL emit a fixed failure event with `storage_worker_poll` correlation and SHALL NOT record the domain error message, exception text, database details, endpoint, credentials, or identifiers inferred from message text

#### Scenario: Worker seeds periodic durable jobs
- **WHEN** a claiming Worker starts or stops the periodic scheduler, or reports an aggregate periodic seed result before any individual job is claimed
- **THEN** the event SHALL use the actual process instance, SHALL use `storage_job_scheduler` for scheduler lifecycle or `storage_job_seed` for seed results, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null rather than inferring ownership from a seed plan, deduplication key, payload, or aggregate count

#### Scenario: Periodic job seeding fails
- **WHEN** periodic plan construction, repository enqueueing, or the seed cycle returns a failure or throws an exception
- **THEN** the scheduler SHALL emit a fixed failure event with `storage_job_seed` correlation and SHALL NOT record the domain error message, exception text, database details, endpoint, credentials, or identifiers inferred from message text
- **AND** the plan builder SHALL normalize caught standard exceptions to the fixed complete `Failed to build periodic storage job plan` error value, while `SeedOnce` SHALL preserve its existing `Failed to build periodic storage jobs` mapping

### Requirement: Typed Process Drain Correlation
The system SHALL correlate process drain lifecycle events without assigning them to an HTTP request or an individual durable job.

#### Scenario: A process begins or completes graceful drain
- **WHEN** an initialized API, Worker, or combined development process begins drain or confirms all owned in-flight work is drained
- **THEN** the lifecycle event SHALL use the actual process instance, SHALL use `process_runtime`, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: A process reaches its drain deadline
- **WHEN** accepted API work, the Worker runtime, or the periodic scheduler remains in flight at the configured deadline
- **THEN** the process SHALL emit a `process_runtime` warning with only bounded aggregate drain state and SHALL NOT repeat the instance in the message or infer request, upload, job, lease, or version ownership

### Requirement: Typed Pre-Instance Process Bootstrap Correlation
The system SHALL correlate process bootstrap events before validated instance registration without inventing process or business ownership.

#### Scenario: A pre-instance bootstrap stage succeeds
- **WHEN** the process starts, initializes libsodium, or loads the runtime configuration before validated instance registration
- **THEN** the application event SHALL use `process_bootstrap` and SHALL keep instance ID, request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: A pre-instance bootstrap stage fails
- **WHEN** libsodium initialization, runtime configuration loading, or secure configuration validation prevents startup
- **THEN** the process SHALL emit a fixed `process_bootstrap` error summary without exception text or inferred ownership and SHALL preserve the existing non-zero exit behavior

### Requirement: Typed Runtime Configuration Diagnostics
The system SHALL emit configuration loading and secure-validation diagnostics with component ownership while keeping them outside request and durable-job correlation.

#### Scenario: Runtime configuration emits a diagnostic
- **WHEN** ConfigMgr reports a loaded/defaulted setting, selected process configuration, validation mode, or bounded validation failure
- **THEN** the event SHALL use `runtime_config`, SHALL use only the instance already registered with the logger or null before registration, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: Configuration values could reveal deployment details
- **WHEN** ConfigMgr reports storage path selection or secure environment validation
- **THEN** the message SHALL NOT contain a configured filesystem path, a manually copied instance ID, an endpoint, credential values, or secret contents, while bounded numeric values, validated backend names, role names, and fixed environment variable names MAY be retained

### Requirement: Typed Storage Runtime Diagnostics
The system SHALL correlate storage backend selection, adapter initialization, manager installation, and recovery-journal setup as process-owned runtime events without assigning request, upload, or durable-job ownership.

#### Scenario: A storage backend or runtime component initializes
- **WHEN** the storage factory selects local or S3 storage, a local/S3 adapter initializes its bounded worker capacity, the process installs its storage managers, or an API configures the S3 recovery journal
- **THEN** the event SHALL use `storage_runtime`, SHALL use only the actual instance registered with the logger, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: Storage deployment details are sensitive
- **WHEN** a storage runtime event reports the selected backend or initialized capacity
- **THEN** the message MAY contain only the bounded backend name and worker-thread or connection counts, and SHALL NOT contain filesystem paths, bucket names, endpoints, regions, object prefixes or keys, credentials, exception text, or a manually copied instance ID

#### Scenario: Storage manager initialization fails
- **WHEN** backend construction or manager installation throws before the process starts serving
- **THEN** the process SHALL emit a fixed `storage_runtime` error without exception text or deployment details and SHALL preserve the existing non-zero exit behavior

#### Scenario: S3 storage factory initialization fails
- **WHEN** `StorageFactory` catches an exception from the S3 client factory, bucket-access validation, or S3 adapter construction
- **THEN** it SHALL rethrow only the fixed complete `Failed to initialize S3 storage backend` text and SHALL NOT append provider error messages, exception text, bucket, endpoint, region, object prefix or key, path, credential, or token values
- **AND** backend selection, bucket validation, construction order, outer fixed runtime diagnostics, and non-zero process exit behavior SHALL remain unchanged

### Requirement: Typed Initialized Process Runtime Diagnostics
The system SHALL correlate post-registration process initialization stages without assigning them to an HTTP request, upload, or durable job.

#### Scenario: An initialized process reports startup progress
- **WHEN** the process initializes token services, records its framework and role, initializes the API application context, enters Worker observation mode, or completes initialization
- **THEN** the event SHALL use `process_runtime`, SHALL use the actual instance registered with the logger, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: Process startup messages expose identity or storage configuration
- **WHEN** the initialized process emits a startup progress event
- **THEN** the message MAY contain a bounded role and framework version but SHALL NOT contain a manually copied instance ID, filesystem path, storage capacity summary, endpoint, credential, secret, token, or exception text

### Requirement: Typed Service Initialization Diagnostics
The system SHALL correlate constructor and singleton-initialization diagnostics from application domain services as process-owned runtime events without assigning HTTP request, upload, or durable-job ownership.

#### Scenario: An application service initializes
- **WHEN** an admin, authentication, cleanup, content, file mutation/query, folder, operation-log, quota, Redis, share, system, trash, upload lifecycle/upload, or user service emits its initialization diagnostic
- **THEN** the event SHALL use fixed `operation=service_runtime`, SHALL accept only the instance already registered with the structured logger, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null

#### Scenario: Service construction has deployment or domain inputs
- **WHEN** a service constructor or singleton initializer receives database, Redis, storage, configuration, user-domain, or other dependency values
- **THEN** its DEBUG message SHALL contain only the fixed low-cardinality service label and SHALL NOT contain a manually copied instance ID, request or domain value, database client name, endpoint, path, credential, secret, token, storage pointer, or exception text; construction order, singleton guards, dependencies, and initialization behavior SHALL remain unchanged

### Requirement: Typed Upload Cache Runtime Diagnostics
The system SHALL correlate the upload-task cache maintenance timer diagnostic as a process-owned upload runtime event without assigning HTTP request, upload, or durable-job ownership.

#### Scenario: The upload-task cache maintenance timer starts
- **WHEN** `UploadService` registers its periodic expired-entry eviction callback with the available event loop
- **THEN** the DEBUG event SHALL use fixed `operation=upload_runtime`, SHALL accept only the instance already registered with the structured logger, and SHALL keep request ID, upload ID, job ID, lease owner, and state version null
- **AND** the message MAY contain only the bounded maintenance interval and SHALL NOT contain a cache key, upload or user identifier, request context, endpoint, path, credential, secret, token, storage pointer, or exception text; event-loop availability, callback registration, eviction behavior, and interval SHALL remain unchanged

### Requirement: Compile-Time Explicit Log Context
The system SHALL require every application logging entry point and direct log-stream construction to receive an explicit typed correlation context so new call sites cannot silently rely on a default empty context.

#### Scenario: Application code emits any supported log level
- **WHEN** code invokes Trace, Debug, Info, Warn, Error, Fatal, high-volume detail, high-volume success, or high-volume failure logging
- **THEN** the public Logger declaration SHALL require exactly one `LogContext` argument, the `LogStream` constructor SHALL require a level and `LogContext`, context-free invocation or construction SHALL be rejected at compile time, and intentionally unowned events SHALL pass an explicit empty `LogContext`
- **AND** severity, high-volume sampling behavior, structured envelope fields, registered instance handling, and JSON null representation for unavailable correlation values SHALL remain unchanged

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

#### Scenario: Expired-trash page cleanup throws
- **WHEN** one expired-trash page catches a database or other standard exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Database error cleaning expired trash page` or `Failed to clean expired trash page` summary with existing typed correlation, and SHALL NOT append cursor, limit, trash, or content values, exception text, SQL, connection details, object locators, paths, credentials, or tokens
- **AND** the existing `InternalError` and `Failed to clean expired trash` result, pagination, chunked permanent deletion, invalid-reference detection, counters, cursor advancement, and trash-first composition SHALL remain unchanged

#### Scenario: Trash list or count query throws
- **WHEN** trash listing catches a database or other standard exception, or trash counting catches a database exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Database error fetching trash list`, `Unknown error fetching trash list`, or `Failed to count trash items` summary with existing typed correlation, and SHALL NOT append user, page, or page-size values, exception text, SQL, connection details, paths, credentials, or tokens
- **AND** the existing list/count `InternalError` and public messages, user scoping, pagination, sorting, response mapping, count query, and controller composition SHALL remain unchanged

#### Scenario: Trash restore or deletion prefetch throws
- **WHEN** the single batch prefetch for trash restoration or permanent deletion catches a database exception before per-item processing
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to batch fetch trash items for restore` or `Failed to batch fetch trash items for delete` summary with existing typed correlation, and SHALL NOT append user or trash IDs, exception text, SQL, connection details, paths, credentials, or tokens
- **AND** the existing batch `InternalError` and public messages, one-shot prefetch, snapshot ownership checks, input order, missing or cross-user item results, transactions, partial-success behavior, cache invalidation, reference counts, and quota SHALL remain unchanged

#### Scenario: Trash permanent-deletion handling throws
- **WHEN** per-item deletion, DeleteAll chunk or outer handling, or permanent-delete rollback catches an exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to permanently delete trash item`, `Failed to process DeleteAll chunk atomically`, `Database error emptying trash`, `Unknown error emptying trash`, or `Trash permanent-delete rollback failed` summary with existing typed correlation, and SHALL NOT append user, trash, or content IDs, exception text, SQL, connection details, paths, credentials, or tokens
- **AND** per-item fixed failures, chunk continuation, outer `InternalError` and public message, rollback rethrow, input order, chunking, partial-success counters, freed-space accounting, transactions, cache invalidation, reference counts, Blob garbage collection, and quota SHALL remain unchanged

#### Scenario: Share list query throws
- **WHEN** share-list counting or paginated loading catches a database exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to get share count` or `Failed to get share list` summary with existing typed correlation, and SHALL NOT append user, status, page, or page-size values, exception text, SQL, connection details, share codes, credentials, or tokens
- **AND** the existing `InternalError` and `Failed to get share list` result, user scoping, active-expiry filtering, other status filters, descending creation-time pagination, batched share-file loading, and response mapping SHALL remain unchanged

#### Scenario: Share creation transaction throws
- **WHEN** share creation catches a database exception from its transaction or a subsequent rollback exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to create share (transaction)` or `Transaction rollback failed` summary with existing typed correlation, and SHALL NOT append user, file, folder, or share IDs, candidate share codes, exception text, SQL, connection details, passwords, credentials, or tokens
- **AND** the existing `InternalError` and `Failed to create share` result, rollback preservation, ownership validation, password hashing, five-attempt uniqueness retry, atomic share and association writes, explicit commit, post-commit fail-open audit, and response mapping SHALL remain unchanged

#### Scenario: Share management write throws
- **WHEN** share settings update, cancellation chunk prefetch, or cancellation batch update catches a database exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to update share`, `Failed to fetch shares for cancel`, or `Failed to cancel share` summary with existing typed correlation, and SHALL NOT append user or share IDs, chunk contents, exception text, SQL, connection details, passwords, credentials, or tokens
- **AND** the existing update error result, cancellation per-item fixed failures and later-chunk continuation, ownership and active-state validation, optional-field updates, password hashing, input order, chunking, duplicate handling, partial-success counters, audits, and response mapping SHALL remain unchanged

#### Scenario: Public share read throws
- **WHEN** shared-folder browsing or download-metadata loading catches a database exception
- **THEN** its directly owned event SHALL use only the matching fixed complete `Failed to browse share folder` or `Failed to get download info` summary with existing typed correlation, and SHALL NOT append share, file, folder, or user IDs, exception text, SQL, connection details, storage locators, credentials, or tokens
- **AND** the existing public errors, active-state validation, root browsing, shared-folder predicates, child ordering, breadcrumbs, single four-table download join, status, expiry, membership and permission checks, Blob mapping, and Range capability SHALL remain unchanged

#### Scenario: Save shared items transaction throws
- **WHEN** saving shared items catches a database or standard exception and its subsequent rollback may also throw
- **THEN** its directly owned events SHALL use only the fixed complete `Failed to save share items` failure summary and `Transaction rollback failed` rollback summary with existing typed correlation, and SHALL NOT append share, file, folder, user, or content IDs, exception text, SQL, connection details, paths, storage locators, credentials, or tokens
- **AND** the existing public error, rollback preservation, share and target validation, recursive copy plan, conflict skips, quota reservation and refund, content reference counts, paths and hierarchy, item and parent counts, explicit commit, post-success cache invalidation, and response mapping SHALL remain unchanged

#### Scenario: Private share helper catches a database exception
- **WHEN** a private share lookup, ownership or active-state validation, single or batched share-file load, or view, download, or file-download metadata update catches a database exception
- **THEN** its directly owned event SHALL use only its existing-prefix fixed complete summary with typed correlation, and SHALL NOT append domain IDs, exception text, SQL, connection details, paths, storage locators, credentials, or tokens
- **AND** lookup and validation error mappings, file-load and statistics fail-open behavior, input deduplication and order restoration, joins, chunking, cross-type association ordering, active-state rules, counter and metadata fields, and caller results SHALL remain unchanged

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

#### Scenario: Upload service dependency handling fails
- **WHEN** `UploadService` catches a database exception while recording chunk metadata or a standard exception while loading the persisted staging session
- **THEN** its directly owned event SHALL use only the corresponding fixed complete `Failed to record chunk upload` or `Failed to load upload staging session` summary with the caller's existing typed correlation and SHALL NOT append upload or user identifiers, exception text, SQL, connection details, object locators, credentials, or tokens
- **AND** the path SHALL retain its existing `InternalError`, public message, staging-write, conditional-record, cache, metric, retry, and cleanup semantics

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

#### Scenario: S3 result becomes a domain error
- **WHEN** a final SDK failure or partial batch-delete failure is converted to `ErrorInfo`
- **THEN** provider response fields SHALL be used only for bounded classification, while the domain message SHALL retain only the fixed SDK operation summary and SHALL NOT contain provider code or message, endpoint, bucket, object key, multipart identifier, signature, or credential

### Requirement: End-to-End Storage Trace Acceptance
The system SHALL provide an environment-gated application-flow acceptance that correlates upload initialization, chunk persistence, completion, download, and asynchronous staging cleanup across the HTTP API, PostgreSQL, the durable Worker, and an S3-compatible endpoint without changing production log volume.

#### Scenario: Isolated S3 application flow is traced
- **WHEN** the acceptance starts one explicitly identified API/Worker process at `DEBUG` against a test-owned Redis/Valkey instance on a reserved local port, sends caller-owned request IDs for init, chunk, complete, and download, and completes one S3-native upload
- **THEN** every response SHALL echo its request ID and actual instance, API and S3 SDK events SHALL retain the corresponding bounded operation, the authoritative upload ID SHALL join PostgreSQL task/chunk rows to the S3 staging prefix, and the completed database content row SHALL retain the observed final object key

#### Scenario: S3 application flow does not inherit shared Redis state
- **WHEN** other integration tests have populated shared authentication, cache, or rate-limit keys before the S3 application acceptance runs
- **THEN** both the generated Redis client configuration and runtime environment SHALL target the test-owned instance, the acceptance SHALL NOT flush or delete shared Redis keys, and cleanup SHALL stop its API/Worker before stopping the owned Redis process on every exit path

#### Scenario: Durable cleanup continues the upload trace
- **WHEN** the completed upload enqueues and a Worker claims its `staging_cleanup` job
- **THEN** the PostgreSQL job's positive ID, aggregate upload ID, and dedupe key SHALL match the Worker start/completion events, S3 cleanup SDK events SHALL use the claimed job, upload, and lease fields, and the successful completion event SHALL clear lease owner while retaining instance and job identity

#### Scenario: Diagnostic trace preserves production sampling
- **WHEN** the environment-gated acceptance enables `DEBUG` to observe expected S3 results and successful chunk detail
- **THEN** the structured application logger SHALL synchronize the level loaded by the framework, that setting SHALL be confined to the test process, normal production `INFO` SHALL continue to suppress those events, and the acceptance SHALL parse only typed schema-v1 application fields rather than extracting identifiers from message text

### Requirement: Background Maintenance Visibility
The system SHALL expose Worker claiming configuration and current acceptance state independently, and a Worker in observation mode SHALL continue to expose dependency readiness and database-backed queue snapshots without executing maintenance work.

#### Scenario: Scheduled cleanup runs
- **WHEN** scheduled maintenance executes
- **THEN** the system SHALL process expired upload or trash state according to the relevant lifecycle rules

#### Scenario: Manual expired-upload cleanup fails
- **WHEN** manual expired-upload cleanup encounters a database or standard processing exception
- **THEN** its directly owned event SHALL use the matching fixed complete failure summary and SHALL NOT append exception text, SQL, connection details, object locators, credentials, or tokens
- **AND** the operation SHALL return the existing internal-error contract without changing trash-first composition, the bounded upload batch, lifecycle work, counters, or correlation

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
