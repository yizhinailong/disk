## Purpose

Defines secure provisioning, build, configuration, migration, service, proxy, observability, backup, restore, upgrade, and rollback operational contracts.

## Requirements

### Requirement: Secure runtime configuration
Deployment documentation SHALL require secure runtime configuration for JWT signing, PostgreSQL password, Redis password, secure-mode startup validation, and production secret injection through environment variables rather than committed configuration files. JWT signing secrets SHALL be at least 32 characters long.

#### Scenario: Missing JWT secret
- **WHEN** the application starts without a JWT secret or with a JWT secret shorter than 32 characters
- **THEN** startup SHALL be rejected according to the secure configuration contract

#### Scenario: Production secret handling
- **WHEN** production credentials are configured
- **THEN** secrets SHALL be supplied through environment variables or protected environment files and SHALL NOT be hard-coded in `config.json`

### Requirement: Deployment prerequisites and build process
Deployment documentation SHALL define supported operating systems, hardware sizing, dependency versions, build tools, CMake presets, and platform-specific build guidance. Supported deployment operating systems SHALL include Ubuntu 22.04 LTS, Ubuntu 20.04 LTS, CentOS 8 Stream, Windows Server 2022, and Windows Server 2019.

#### Scenario: Linux deployment prepared
- **WHEN** an operator prepares a Linux deployment
- **THEN** the documentation SHALL identify required packages, PostgreSQL and Redis dependencies, build presets, and expected directory layout

#### Scenario: Windows deployment prepared
- **WHEN** an operator prepares a Windows deployment
- **THEN** the documentation SHALL identify Windows Server 2022 or Windows Server 2019 as supported and SHALL describe Windows service-management expectations without changing backend behavior

### Requirement: Database and cache operations
Deployment documentation SHALL define PostgreSQL setup, schema initialization, performance configuration, Redis setup, Redis authentication, connection validation, connection-pool sizing, and common connectivity troubleshooting.

#### Scenario: PostgreSQL initialized
- **WHEN** a new PostgreSQL environment is created
- **THEN** the operator SHALL be able to create the database/user, grant required privileges, and run the documented initialization script

#### Scenario: Redis validated
- **WHEN** Redis is configured for the application
- **THEN** the operator SHALL be able to verify service status, authentication, connection count, and application configuration alignment

### Requirement: PgBouncer transaction-pool compatibility
Deployment documentation MAY claim PgBouncer transaction-pool compatibility only for PgBouncer 1.25.2 or later with `pool_mode=transaction`, non-zero protocol-level `max_prepared_statements`, and `server_reset_query_always` disabled. PgBouncer SHALL continue to target the single stable PostgreSQL writer endpoint. Application client-pool limits, PgBouncer backend-pool limits, PostgreSQL connection budgets, authentication, and TLS SHALL remain independently bounded and verified.

#### Scenario: Transaction pooling is admitted
- **WHEN** two real API processes connect through the documented PgBouncer transaction pool
- **THEN** client connections SHALL be multiplexed onto fewer bounded server connections while cross-instance ORM operations, a multi-statement Drogon transaction, and protocol-level named prepared statements complete successfully

#### Scenario: A transactional advisory lock is exercised
- **WHEN** one client holds `pg_advisory_xact_lock` through PgBouncer and another client tries the same lock
- **THEN** the second client SHALL be excluded before the first transaction commits and SHALL acquire the lock after commit

#### Scenario: SQL depends on session identity
- **WHEN** application or operational SQL introduces a session-level advisory lock, SQL-level prepared statement, LISTEN state, holdable cursor, persistent temporary table, or session SET/RESET dependency
- **THEN** the repository compatibility contract SHALL fail and transaction pooling SHALL NOT be enabled until the dependency is removed and the real compatibility gate passes again

### Requirement: Capacity-derived distributed defaults
The distributed deployment profile SHALL fix conservative per-process starting values derived from the recorded capacity and failure baselines. Each application process SHALL use 4 HTTP threads, 8 PostgreSQL connections, 4 Redis connections, 16 S3 HTTP connections, and 4 S3 I/O threads. Each API process SHALL use 4 authentication CPU threads and an assembly concurrency limit of 2. Worker execution concurrency SHALL remain 1. The recommended highly available starting topology SHALL be 2 API replicas and 2 Worker replicas.

#### Scenario: Distributed profile is rendered
- **WHEN** an operator uses the repository distributed deployment artifacts without capacity overrides
- **THEN** the fixed per-process pools, concurrency limits, and 2 API plus 2 Worker topology SHALL match the documented starting profile

#### Scenario: Replica count is increased
- **WHEN** an operator proposes one or more API/Worker replica steps
- **THEN** the repository capacity gate SHALL derive per-process values from the distributed runtime configuration, calculate every named step, require explicit PostgreSQL, Redis, S3 HTTP, and S3 I/O budgets, retain one current application process as rolling-replacement reserve, and emit schema-versioned JSON evidence before rollout

#### Scenario: A replica step exceeds a dependency budget
- **WHEN** any proposed step plus its operational reserve exceeds a supplied PostgreSQL, Redis, S3 HTTP, or S3 I/O budget
- **THEN** the capacity gate SHALL fail without marking the plan accepted and the operator SHALL stop before changing replica counts

#### Scenario: Capacity moves beyond the measured matrix
- **WHEN** API or Worker replicas exceed 4, a local pool or concurrency limit is raised, or the production object-store implementation differs from the recorded baseline
- **THEN** the repository gate SHALL reject the existing capacity plan and a representative capacity test SHALL be completed before the measured boundary or production recommendation is changed

### Requirement: Incremental database migration procedure
Deployment documentation SHALL define a manifest-ordered, checksum-verified forward migration procedure for documented database migrations. A single migration Job SHALL run the repository migration entry point before application rollout; API and Worker startup SHALL validate schema compatibility without executing DDL. Each manifest entry SHALL acquire the migration lock, apply in one transaction, and record its version and checksum. A failed or partially completed run SHALL preserve every committed expand migration and SHALL be safe to resume after correction.

#### Scenario: Migration starts
- **WHEN** an operator starts a database migration
- **THEN** the procedure SHALL require an identified V002-compatible baseline, a completed backup and isolated restore rehearsal, reviewed manifest checksums, and passing upgrade compatibility gates before forward SQL is applied by one migration Job

#### Scenario: Two migration Jobs overlap
- **WHEN** two migration Jobs accidentally evaluate the same unapplied manifest entry
- **THEN** the transactional advisory lock and migration ledger SHALL prevent duplicate schema effects and both Jobs SHALL reject any checksum drift

#### Scenario: Reconciliation fails
- **WHEN** migration-ledger, schema, data, health, or object reconciliation reports a mismatch
- **THEN** the release SHALL stop, every committed expand object and ledger row SHALL remain intact, and operators SHALL correct forward and resume rather than run destructive rollback SQL

#### Scenario: Application validation fails after migration
- **WHEN** a candidate application fails health or functional validation after the expand migration commits
- **THEN** application processes, configuration, or traffic SHALL roll back only to a release proven compatible with the expanded schema, while the schema action remains `preserve_expand`

#### Scenario: Backup restoration is required
- **WHEN** corruption or loss requires disaster recovery rather than ordinary application rollback
- **THEN** the backup SHALL first restore into an empty isolated database and pass migration-ledger, schema, data, and object reconciliation before a separately approved traffic switch

### Requirement: Read-only final Blob migration manifest
Before copying any legacy local final Blob to object storage, the migration tool SHALL create a complete content-ID-ordered inventory from one PostgreSQL repeatable-read, read-only snapshot. Each record SHALL preserve the database locator and SHALL include the normalized local-root-relative source path, size, MD5, SHA-256, and canonical target key. Manifest generation SHALL require neither S3 access nor S3 credentials and SHALL NOT modify PostgreSQL, source Blobs, object storage, or a migration checkpoint.

#### Scenario: A final Blob inventory is generated
- **WHEN** an operator runs only the manifest command against a stopped-writer local final store
- **THEN** every `file_contents` row, including a zero-reference row awaiting garbage collection, SHALL appear once in content ID order, every required field and canonical target key SHALL match the snapshot, and the manifest SHALL be published with mode `0600`

#### Scenario: The manifest target already exists
- **WHEN** the requested final path exists before generation or appears concurrently with publication
- **THEN** the command SHALL fail without replacing or changing that file and without creating an S3 request or checkpoint

#### Scenario: Manifest validation fails after temporary output begins
- **WHEN** a later source row is missing, invalid, outside the trusted root, or inconsistent with the database snapshot
- **THEN** the command SHALL fail without changing PostgreSQL or any source Blob and SHALL leave neither a final partial manifest nor a temporary manifest artifact

### Requirement: Resumable and verified final Blob copy
The final Blob copy command SHALL default to a non-writing dry run and SHALL verify each local source and each existing or newly uploaded target by size, MD5, and SHA-256. Copy and cutover SHALL use migration-specific S3 credential variables or a distinct migration workload identity and SHALL reject application S3 credential variables. The migration identity SHALL be limited to read, write, and multipart operations under the final-object namespace, without staging, delete, object-version purge, or bucket-administration permission. Manifest and rollback SHALL require no S3 access. Execution SHALL commit a durable, exclusively locked per-object checkpoint only after a complete target GET passes, and SHALL bind that checkpoint to the exact manifest SHA-256, bucket, and object prefix. A positive transfer-rate setting SHALL apply one aggregate byte budget to S3 upload callbacks and complete target GET verification; zero SHALL disable the limit and a negative setting SHALL be rejected.

#### Scenario: Application credentials are injected into the migration job
- **WHEN** copy or cutover receives application S3 credential variables instead of an isolated migration identity
- **THEN** the command SHALL fail before any S3 request, checkpoint mutation, or database path change

#### Scenario: A bounded rate-limited copy batch runs
- **WHEN** an operator executes a one-object batch with a positive transfer budget against an absent target
- **THEN** exactly one object SHALL be uploaded and completely downloaded for verification, exactly one checkpoint record SHALL commit, and elapsed time SHALL NOT be less than the charged upload-plus-GET bytes divided by the configured budget

#### Scenario: Copy runs as a dry run
- **WHEN** the copy command is invoked without its execution flag
- **THEN** it SHALL still validate local sources and inspect targets, but SHALL perform no PUT, create no checkpoint, and change no database locator

#### Scenario: Copy is interrupted during an object
- **WHEN** the process is killed after a target PUT begins but before its complete verification and checkpoint commit
- **THEN** the checkpoint SHALL contain only earlier completely verified objects and a subsequent execution SHALL revalidate those objects and process the interrupted and remaining objects

#### Scenario: A completed copy is replayed
- **WHEN** the same manifest and checkpoint are executed again after all targets were verified
- **THEN** every target SHALL be completely revalidated, no target SHALL be uploaded again, and the checkpoint binding SHALL remain valid

#### Scenario: A source or target is corrupt
- **WHEN** a source or existing target differs in size, MD5, or SHA-256 from its manifest record
- **THEN** the object SHALL fail independently before a conflicting target is overwritten or a verification checkpoint is committed

### Requirement: Verified and atomic final Blob database cutover
The final Blob cutover command SHALL open the checkpoint read-only, require one exact bound verification record for every manifest object, and completely re-read every target to verify size, MD5, and SHA-256 before it may enter the database path-switch transaction. The transaction SHALL lock `file_contents` with `ACCESS EXCLUSIVE NOWAIT`, classify the complete database set against the manifest, and change every source locator to its canonical target key atomically. Cutover SHALL NOT upload or repair an object.

#### Scenario: The checkpoint is absent or incomplete
- **WHEN** cutover is requested before copy has committed a verification record for every manifest object
- **THEN** cutover SHALL fail before changing any database locator even if some target objects already exist

#### Scenario: A verified target changes before cutover
- **WHEN** an object recorded in the checkpoint is missing or no longer matches its manifest size, MD5, or SHA-256 during cutover revalidation
- **THEN** cutover SHALL fail without trusting the stale checkpoint and every database locator SHALL remain unchanged

#### Scenario: The database snapshot drifts
- **WHEN** the locked `file_contents` set has an added, removed, metadata-changed, or mixed source/target row relative to the manifest
- **THEN** the path-switch transaction SHALL fail without changing any preceding matching row

#### Scenario: Every object and database row passes
- **WHEN** all checkpoint records and complete target reads pass and the locked database set is wholly at the manifest source state
- **THEN** one transaction SHALL change exactly every manifest locator to its canonical target key, while a replay against the wholly target state SHALL change zero rows

### Requirement: Bounded final Blob maintenance-window strategy
Because the current schema has no per-content final-storage backend, local-to-S3 final Blob migration SHALL use a stopped-writer maintenance window with a hard maximum duration and no extension. The reviewed policy SHALL require named migration, database, and rollback owners; an ordered freeze, backup, manifest, copy, atomic cutover, probe, reconciliation, and traffic-open sequence; and explicit stop conditions. It SHALL forbid online partial database cutover, online dual write, and unbounded dual read.

#### Scenario: A maintenance window is approved
- **WHEN** operators prepare a target-environment final Blob migration
- **THEN** they SHALL record UTC start and end times within the reviewed maximum duration, assign all required owners, validate the reviewed policy, stop ingress, every API and Worker, scheduled maintenance, and Blob GC before inventory, and keep ingress closed until the traffic-open gate

#### Scenario: The window expires before database cutover
- **WHEN** the approved window expires while copy or verification is incomplete and database locators still reference local Blobs
- **THEN** operators SHALL stop without opening traffic, retain the manifest, verified targets, checkpoint, database backup, and local sources, and resume from the checkpoint only in a newly approved window

#### Scenario: The window expires or validation fails after database cutover
- **WHEN** database locators were atomically switched but the S3 application configuration, download and Range probes, or full reconciliation have not all passed before expiry
- **THEN** operators SHALL keep traffic closed and restore all original locators from the same manifest before any local source is removed

#### Scenario: An unbounded migration mode is proposed
- **WHEN** a plan enables online partial cutover, dual write, an extension of the maintenance window, or dual read without a deadline
- **THEN** the policy gate SHALL reject the plan before migration execution

### Requirement: Deferred legacy local Blob retirement
The reviewed final Blob migration policy SHALL authorize only scheduling, not executing, retirement of legacy local sources. Scheduling SHALL remain blocked until the cutover evidence is accepted, the rollback window is closed, a post-cutover coordinated recovery set and its manifest digest are verified through an isolated restore drill, download and Range probes pass, every page of `contents`, `users`, `staging`, and `final` reconciliation succeeds, all reconciliation jobs and findings plus quota and reference-count mismatches are zero, the local source inventory still matches the migration manifest, and the manifest and checkpoint are archived. The earliest retirement date SHALL be at least 30 days after all prerequisites pass.

#### Scenario: Backup or reconciliation evidence is incomplete
- **WHEN** a retirement proposal lacks the post-cutover recovery set, isolated restore acceptance, any reconciliation scope or page, a zero blocker result, or the exact retained source inventory
- **THEN** the policy gate SHALL reject the proposal and every local source SHALL remain available

#### Scenario: A retirement schedule is admitted
- **WHEN** all reviewed prerequisites have passed, the rollback window is closed, the minimum retention interval has elapsed, and the storage, backup, and rollback owners approve a target-environment schedule
- **THEN** the gate MAY admit that schedule while producing no delete command and changing no local source

#### Scenario: Destructive retirement is requested
- **WHEN** an operator is ready to quarantine or delete the scheduled local sources
- **THEN** a separate target-environment destructive change SHALL revalidate the exact manifest inventory, recovery-set retention, and current S3 reconciliation; the repository scheduling gate SHALL NOT perform the deletion

### Requirement: Service management and hardening
Deployment documentation SHALL define service installation, systemd or Windows service configuration, filesystem permissions, sandboxing/hardening settings, restart behavior, and routine service operations.

#### Scenario: Service installed on Linux
- **WHEN** the application is installed as a Linux service
- **THEN** it SHALL run under a dedicated service account with configured working directory, environment file, restart policy, and write access limited to documented data/log paths

### Requirement: HTTPS and reverse proxy operations
Deployment documentation SHALL define HTTPS certificate options, reverse proxy settings, upload body-size limits tied to chunk size, timeout requirements, and download buffering considerations. In the reviewed Nginx topology, public TLS SHALL terminate once at Nginx while application instances continue to listen on private HTTP endpoints. The container and production TLS entry points SHALL consume the same reviewed upstream and proxy-server policy so routing, forwarding, buffering, timeout, metrics-deny, and retry behavior cannot drift.

#### Scenario: Nginx front end configured
- **WHEN** Nginx proxies requests to the backend
- **THEN** the configuration SHALL preserve host, client address, forwarded protocol, and request ID headers, support long upload/download operations, and set request body limits based on upload chunk size rather than maximum logical file size

#### Scenario: Public TLS terminates at Nginx
- **WHEN** the production TLS entry point is installed
- **THEN** HTTP SHALL redirect to HTTPS without changing the request target, only Nginx SHALL bind the public HTTPS port, and API instances SHALL remain reachable only through documented private HTTP endpoints

#### Scenario: A streamed non-idempotent request loses its upstream
- **WHEN** Nginx has begun forwarding a POST, PUT, or PATCH request body and the selected API becomes unavailable
- **THEN** the proxy SHALL NOT automatically replay that request on another API, and client recovery SHALL use the documented idempotent business operation

### Requirement: Monitoring, logging, backup, restore, and troubleshooting
Deployment documentation SHALL define log rotation, health checks, service/database/cache monitoring, database and file backups, restore procedures, and troubleshooting paths for startup, database, Redis, upload, CPU, memory, and disk I/O issues. If the Prometheus plugin is enabled, deployment documentation SHALL identify the `/metrics` endpoint as the metrics scrape hook.

#### Scenario: Routine health check
- **WHEN** an operator verifies a running deployment
- **THEN** they SHALL be able to check service status, listening port, disk usage, PostgreSQL connectivity, Redis connectivity, and `/api/health`

#### Scenario: Scheduled backup configured
- **WHEN** a production deployment is prepared
- **THEN** database and file backups SHALL have documented commands, retention expectations, and restore steps

#### Scenario: PostgreSQL point-in-time recovery is accepted
- **WHEN** a release claims a PostgreSQL recovery-point objective backed by physical backups and continuous WAL archiving
- **THEN** a manifest-verified, unmodified base backup SHALL be copied into a fresh isolated data directory and recovered through one explicit time, LSN, transaction, or named target without modifying the source cluster
- **AND** acceptance SHALL prove the expected committed state on both sides of the target, unchanged system identity, a new promoted timeline, continuous required WAL, a matching recoverable final-object snapshot, full reconciliation, and measured RPO/RTO evidence before traffic is allowed

#### Scenario: Isolated restore drill completed
- **WHEN** backup readiness is accepted for a release
- **THEN** a coordinated database and final-object recovery set SHALL be restored into isolated empty resources and SHALL pass schema, quota, reference-count, object-integrity, paginated reconciliation, failure-blocking, and repaired-rescan checks before traffic is allowed

### Requirement: Least-privilege object-storage provisioning
Distributed deployment artifacts SHALL provision distinct staging and final key namespaces, a lifecycle policy that cannot expire final objects, and an application identity limited to required data-plane operations in those namespaces. A separate revocable migration identity SHALL be limited to read, write, and multipart operations in the final namespace and SHALL NOT receive staging, delete, object-version purge, or bucket-administration permission. The repository MinIO sample bucket SHALL have versioning enabled and verified by the provisioning identity before application access is admitted; this SHALL NOT grant either data-plane identity bucket-versioning administration or object-version purge permissions. Object-store root or administrative credentials SHALL be confined to provisioning and SHALL NOT be injected into API or Worker processes. API and Worker processes SHALL receive only application credentials, while migration jobs SHALL receive only migration credentials; the migration identity SHALL be revoked after the approved migration window. Application S3 dependency failures and reconciliation findings SHALL be monitored, while provider-native availability, capacity, replication, healing, encryption, transport security, and lifecycle monitoring remain required for the target object store.

#### Scenario: MinIO sample is provisioned
- **WHEN** the repository MinIO initializer runs with separate root, application, and migration credentials
- **THEN** it SHALL create the bucket, idempotently enable and verify bucket versioning, import and verify lifecycle rules, idempotently provision and bind both scoped policies, and leave API and Worker processes configured only with the application credentials

#### Scenario: Application S3 credentials are exercised
- **WHEN** the application identity accesses the sample bucket
- **THEN** required list, object, copy-compatible, delete, and multipart operations under `objects/*` and `staging/*` SHALL succeed while access outside those namespaces, bucket-versioning administration, object-version purge, and other bucket administration SHALL be denied

#### Scenario: Migration S3 credentials are exercised and revoked
- **WHEN** the migration identity accesses the sample bucket and the approved migration window ends
- **THEN** final-object read, write, and multipart operations SHALL succeed before revocation while staging access, deletion, listing, bucket administration, and application credential reuse are denied, and all access with that identity SHALL fail after revocation

### Requirement: Transitional upload release
The compatibility release immediately after the expand migration SHALL default newly created upload sessions to local staging while retaining support for both persisted local and S3 staging descriptors. Existing tasks SHALL always select storage from their persisted backend and prefix rather than the process's current default. The final distributed S3 configuration SHALL NOT be enabled during this compatibility step.

#### Scenario: Compatibility release starts without a staging override
- **WHEN** the compatibility release starts from the standard repository configuration
- **THEN** new upload sessions SHALL persist the local backend and an immutable local session locator

#### Scenario: Compatibility release handles an existing task
- **WHEN** a request or Worker loads a legacy local task or a task with a persisted S3 descriptor
- **THEN** it SHALL use that task's persisted compatible locator and SHALL NOT reinterpret it using the current process default

### Requirement: Legacy local staging drain
The rollout SHALL treat legacy local staging as original-volume-affine state because its persisted descriptor does not identify a physical node or volume. Operators SHALL inventory the actual volume owner, preserve request affinity, and allow each task to complete, expire naturally, or be cancelled in a maintenance window through a compatible API and the single migration Worker mounted to that volume. Database terminal counts and a read-only scan of every inventoried volume SHALL both reach zero before affinity is removed.

#### Scenario: A local task is inspected from different nodes
- **WHEN** the same legacy local task is diagnosed through an API mounted to its original volume and an API mounted to another empty volume
- **THEN** the original-volume API SHALL report the staged chunk present, the other API SHALL report it missing, and both diagnostics SHALL leave task, chunk, lease, quota, and job state unchanged

#### Scenario: Legacy local staging is drained
- **WHEN** the migration window processes inventoried local tasks through their original volumes
- **THEN** representative tasks SHALL converge through completion, cancellation, and natural expiration, their cleanup jobs SHALL succeed on the original-volume Worker, reserved quota and chunk metadata SHALL be released, and unrelated volumes SHALL remain untouched

#### Scenario: A local task has no proven volume owner
- **WHEN** a database row is missing its staged artifacts, appears on multiple volumes, or cannot be mapped to exactly one original volume
- **THEN** operators SHALL freeze and reconcile it rather than claim that another arbitrary API or Worker can recover or clean it

### Requirement: Production local staging creation cutoff
After local non-terminal uploads, incomplete local cleanup jobs, and staged artifacts on every inventoried original volume have all reached zero, the cutover release SHALL reject a secure-mode API configured with local upload staging before it opens a listener. A secure-mode Worker MAY retain local staging access only to finish verified legacy cleanup; non-secure local staging SHALL remain limited to development, tests, and an isolated migration environment.

#### Scenario: A production API is configured with local staging
- **WHEN** an `api` process starts with secure mode enabled and `upload_staging_backend=local`
- **THEN** startup SHALL fail before the process accepts traffic and no new local upload task can be created

#### Scenario: A legacy cleanup Worker retains the original volume
- **WHEN** an explicit `worker` process starts in secure mode with local staging after API creation has been cut off
- **THEN** configuration validation SHALL allow the Worker to process persisted legacy descriptors without reopening the upload creation path

#### Scenario: A developer runs the local backend
- **WHEN** secure mode is disabled and local staging is selected
- **THEN** the existing single-process development and test workflow SHALL remain available and SHALL NOT be represented as a production topology

#### Scenario: Production workloads have no authoritative node-local storage
- **WHEN** the reference production API and Worker workloads are rendered
- **THEN** they SHALL enable secure mode with both final and upload staging storage set to S3, SHALL NOT mount a host path or persistent volume for Blob or upload-staging correctness, and MAY use only explicitly disposable local runtime scratch that can be lost without moving sessions or files before replacement

### Requirement: Reversible dual-API admission
The distributed release SHALL provide a reviewed two-API load-balancer pool and a one-API fallback pool that removes the newly admitted instance without changing database schema, persistent application state, or client affinity. The switch operation SHALL validate the complete deployment configuration before applying it, and an instance SHALL be removed from the active pool and verified absent from routed traffic before it receives a termination signal. This admission gate does not replace the later non-sticky randomized-routing acceptance test.

#### Scenario: A second API is admitted
- **WHEN** the new `api-b` instance has passed its direct readiness probe and identifies itself as `disk-api-b`
- **THEN** operators SHALL apply the validated two-API pool and keep the one-API pool available as the immediate rollback configuration

#### Scenario: The new API is removed quickly
- **WHEN** the new instance must be rolled back
- **THEN** operators SHALL apply the validated `api-a`-only pool, repeatedly verify through the load balancer that successful probes identify only `disk-api-a`, and only then stop or terminate `api-b`

#### Scenario: A pool configuration is invalid
- **WHEN** validation of either reviewed pool fails
- **THEN** the load balancer SHALL NOT be recreated or reloaded from that configuration and the currently active pool SHALL remain the recovery point

### Requirement: Non-sticky routed upload acceptance
The active two-API pool SHALL accept a single upload whose requests are dynamically routed without client affinity. Runtime acceptance SHALL use one persistent HTTP client, one fixed cookie value, and only the load-balancer address for upload initialization, intentional same-chunk retries, completion, and download. It SHALL record the application-provided `X-Disk-Instance-Id` response header for every step, observe both reviewed API instance IDs before completion, and verify one completed file, one content reference, correct quota settlement, and byte-identical download. Static configuration inspection and requests sent directly to chosen API ports SHALL NOT satisfy this gate.

#### Scenario: One client reaches both APIs
- **WHEN** a client keeps the same connection context and cookie while retrying an immutable upload chunk through the active load-balancer pool
- **THEN** successful responses SHALL identify both `disk-api-a` and `disk-api-b`, every retry SHALL converge on the same authoritative chunk, and no route-selection cookie or client-controlled instance hint SHALL be required

#### Scenario: The randomly routed upload completes
- **WHEN** the same client completes and downloads that upload only through the load-balancer address
- **THEN** the task SHALL reach `Completed`, its completed file and final object SHALL be unique, reserved quota SHALL be settled exactly once, and the downloaded bytes SHALL match the original payload

#### Scenario: Runtime routing remains affine
- **WHEN** all bounded route probes for the same client and upload identify only one API while both reviewed targets are ready
- **THEN** the acceptance gate SHALL fail even if static configuration contains no cookie, sticky, or IP-hash directive

#### Scenario: Reference ingress has no affinity configuration
- **WHEN** the repository production Service and reverse-proxy policy are rendered
- **THEN** the Service SHALL use no session affinity and the proxy SHALL contain no cookie, sticky, or IP-hash routing directive, while this static result SHALL NOT replace the runtime two-instance acceptance

### Requirement: Worker observation rollout
The rollout SHALL support deploying a Worker with job claiming disabled after the expand migration and compatibility release. The observation Worker SHALL validate its required dependencies and query real queue metrics without claiming, renewing, completing, or seeding persistent jobs. Enabling execution SHALL require an explicit configuration change and process restart.

#### Scenario: Observation Worker is deployed
- **WHEN** a Worker starts with `worker_claiming_enabled=false`
- **THEN** readiness SHALL succeed when its database, task schema, and storage dependencies are healthy, health and metrics SHALL identify the non-claiming state, and queued jobs SHALL remain unchanged across polling intervals

#### Scenario: Observation gate fails
- **WHEN** dependency readiness or queue snapshot collection fails, an observation instance appears as a lease owner, a queued job changes because of that instance, or cluster maintenance jobs are unexpectedly seeded
- **THEN** the rollout SHALL stop before Worker execution is enabled

#### Scenario: Worker execution is enabled
- **WHEN** the observation gate has passed and the rollout advances to task execution
- **THEN** the operator SHALL explicitly set job claiming enabled and restart or redeploy the Worker rather than treating the startup setting as a live pause control

#### Scenario: API capacity changes after Worker execution cutover
- **WHEN** a claiming Worker has seeded the current UTC window and API-only replicas are subsequently added or removed
- **THEN** only the Worker SHALL report a running periodic seeder, the current window SHALL contain one `expire_uploads`, one `expire_trash`, and four first-page `storage_reconcile` jobs, and the complete persisted rows for those jobs SHALL remain unchanged by the API capacity change

#### Scenario: Scheduler ownership cutover fails
- **WHEN** an API-only process reports effective Worker claiming, logs periodic seeder startup or seed-cycle activity, or an API scale event creates or mutates a periodic job
- **THEN** the rollout SHALL stop before additional API or Worker replicas are enabled

### Requirement: Worker rollback drains before lease takeover
The rollout SHALL retire claiming Workers one at a time while at least one compatible successor is ready. A termination signal SHALL stop new claim polls and periodic seeding before process exit, and the orchestrator termination grace SHALL exceed the configured application drain timeout. A job that cannot finish within that timeout SHALL retain its persisted `Running` owner and `locked_until`; operators SHALL NOT clear, shorten, or otherwise rewrite the lease to accelerate rollback.

#### Scenario: A claiming Worker is retired
- **WHEN** a Worker receives `SIGTERM` while a compatible successor remains ready
- **THEN** the retiring process SHALL report `draining=true`, configured claiming enabled, current acceptance disabled, and failed readiness before exiting, and it SHALL NOT start another storage job or seed cycle after the drain boundary

#### Scenario: Held work exceeds the drain timeout
- **WHEN** an in-flight handler is still blocked when the bounded drain timeout expires
- **THEN** the process SHALL exit, its last persisted lease SHALL remain live until PostgreSQL time reaches `locked_until`, and the successor SHALL claim it only after that deadline with an incremented attempt and lease-takeover outcome

#### Scenario: Worker rollback violates the lease boundary
- **WHEN** the orchestrator kills the process before its application drain timeout, a retiring instance starts new work after drain, a successor claims a live lease, or an operator mutates owner/deadline fields
- **THEN** the rollback SHALL stop and SHALL NOT retire another Worker until the deployment and persisted task state are reconciled

### Requirement: Concurrent Workers use exclusive queue ownership
When multiple claiming Workers are ready, PostgreSQL queue ownership SHALL assign each job row to at most one live owner at a time. Successful handlers SHALL persist one terminal result per logical job while idempotent retries remain safe after an owner failure.

#### Scenario: Two Workers compete for blocked jobs
- **WHEN** two single-concurrency Workers claim the same ready batch while two independent Blob GC handlers are held open by database row locks
- **THEN** the two `Running` rows SHALL have distinct Worker owners and one attempt each, and neither Worker SHALL claim both execution slots

#### Scenario: Both blocked jobs are released
- **WHEN** the database locks are released while both Worker leases remain live
- **THEN** each job SHALL reach `Succeeded` with one attempt, clear its owner and lease, retain one deduplicated queue row, and remove its zero-reference content row and Blob exactly once

### Requirement: Lost upload-completion responses use business replay
The rollback and incident runbook SHALL treat a missing or failed HTTP response as an unknown outcome, preserve the original `upload_id`, and retry `POST /api/file/upload/complete` through a compatible API before considering any administrative recovery. A completed replay SHALL be verified through the returned file ID and read-only upload diagnostics. The runbook SHALL prohibit direct database state changes and manual deletion of final S3 objects or unverified staging objects.

#### Scenario: Completion outcome is unknown
- **WHEN** the client connection fails after a complete request may have reached an API
- **THEN** operators SHALL retain the request and upload identifiers, retry complete with the same authenticated owner and `upload_id`, and accept the same completed file result without object surgery

#### Scenario: Replay reports an active finalization lease
- **WHEN** the retry returns the documented finalization conflict
- **THEN** operators SHALL inspect the upload through the read-only diagnostic endpoint and retry after the active owner finishes or the lease expires, using audited lease release only when the owner is confirmed dead

### Requirement: S3 staging activation gate
After incompatible old application versions have exited and the Worker observation gate has passed, the distributed deployment SHALL expose an explicit startup setting that selects S3 staging for newly created upload sessions. Creating a session SHALL atomically persist its selected backend and exact prefix, and later requests or Workers SHALL continue to use that persisted descriptor independently of the process's current default.

#### Scenario: S3 staging is enabled for new sessions
- **WHEN** a current API process starts with S3 final storage and the upload staging setting explicitly set to S3
- **THEN** each newly initialized non-instant upload SHALL persist `staging_backend=s3` and the configured staging prefix followed by its upload ID before any chunk is accepted

#### Scenario: The startup setting changes
- **WHEN** the staging setting is changed and API processes are restarted or redeployed
- **THEN** only sessions created after that process startup SHALL use the new default, while every existing local or S3 session SHALL retain its persisted backend and prefix

#### Scenario: A small S3 staging cohort is enabled
- **WHEN** a controlled deployment routes selected upload initialization requests to an S3-default API while baseline APIs still default to local staging
- **THEN** only the selected new sessions SHALL persist S3 staging, subsequent requests MAY use any compatible API and SHALL follow the persisted descriptor, and the cohort route SHALL NOT trust a client-controlled opt-in header

#### Scenario: The S3 staging cohort is observed
- **WHEN** the canary cohort handles uploads
- **THEN** operators SHALL compare completion success and latency, S3 dependency outcomes, expired leases and takeovers, dead-letter jobs, metric snapshot health, and unresolved reconciliation findings against the baseline before increasing the cohort

#### Scenario: The S3 staging cohort is progressively expanded
- **WHEN** every prior observation gate has passed and initialization traffic advances through controlled intermediate cohorts to 100 percent S3 staging
- **THEN** each stage SHALL affect only sessions initialized in that stage, all previously persisted backend and prefix descriptors SHALL remain byte-for-byte unchanged, and local sessions SHALL retain their required original-volume affinity until terminal cleanup

#### Scenario: An expansion stage fails
- **WHEN** an expansion stage breaches a stop condition or changes any descriptor captured before that stage
- **THEN** operators SHALL stop expansion, route subsequent initialization requests back to the last passing cohort, and keep compatible local and S3 handlers available for every session already created

### Requirement: Every staging rollout stage has an executable rollback contract
The S3 staging rollout SHALL use the ordered percentages `10, 25, 50, 100` without skipping an intermediate stage. Before any stage changes traffic, a reviewed machine-readable plan SHALL bind that stage to its immediately preceding percentage, minimum observation window and non-instant upload sample, complete quantitative stop-condition set, release and rollback owner roles, exact preview/apply/rollback commands, and named read-only validation queries. A target change record SHALL supply actual named people for every required role and an approved rollout adapter; repository role names or placeholders SHALL NOT count as assigned owners. The adapter SHALL compare the declared current percentage with target deployment state and SHALL be limited to routing and process defaults for newly initialized uploads; it SHALL NOT mutate persisted tasks or inherit database/object-store credentials.

#### Scenario: A stage is prepared
- **WHEN** initialization traffic is about to move from the last passing percentage to the next reviewed percentage
- **THEN** the operator SHALL validate the reviewed plan, record the change ID and named release, rollback, database-verification, and storage-verification owners, preview the exact adjacent transition, capture the pre-stage descriptor snapshot, and only then execute the approved rollout command

#### Scenario: A stage is accepted
- **WHEN** the minimum observation window and sample have completed without a stop condition
- **THEN** all named validation queries SHALL run in a repeatable-read read-only transaction, the pre-stage descriptor digest SHALL remain unchanged, and the results and command output SHALL be retained before the next stage is attempted

#### Scenario: A stage is stopped or rolled back
- **WHEN** any quantitative stop condition fires, any owner or evidence is missing, a query cannot complete read-only, or a non-adjacent transition is requested
- **THEN** expansion SHALL stop and the named rollback owner SHALL use the reviewed command to return new initialization traffic to the immediately preceding passing percentage without updating upload rows or deleting local or S3 objects

#### Scenario: S3 staging activation fails its gate
- **WHEN** a new task persists the wrong backend or prefix, an S3 task creates node-local staging data, required S3 operations fail, or an existing task descriptor changes
- **THEN** the rollout SHALL stop and MAY restore local staging for subsequently created tasks, but SHALL keep a compatible release available to finish or explicitly cancel already-created S3 tasks

### Requirement: Upload task creation rollback cutoff
Before an application rollback removes S3 staging or new upload-state support, operators SHALL close the startup upload-task creation cutoff on every compatible API instance and verify that novel non-instant initialization is rejected without task or quota mutation. Closing the cutoff SHALL NOT rewrite a task descriptor, migrate staging state, stop existing lifecycle requests, or make an incompatible old binary safe. Upload lifecycle traffic for every persisted S3 task or new-schema state SHALL remain isolated to compatible handlers until the task is terminal or explicitly frozen under the documented recovery procedure.

#### Scenario: The rollback cutoff is closed
- **WHEN** all compatible API instances restart with `upload_task_creation_enabled=false`
- **THEN** a novel non-instant initialization SHALL fail with HTTP 503 and code `50012`, while resumable initialization and the chunk, complete, and cancel operations for existing tasks SHALL remain available through compatible instances

#### Scenario: A persisted S3 task exists during rollback
- **WHEN** an upload task has `staging_backend=s3`, a persisted S3 prefix, `Finalizing`, or another state unknown to the old release
- **THEN** load-balancer and Worker routing SHALL exclude the old release from that task's lifecycle and SHALL NOT rewrite the task to local staging or a legacy state

#### Scenario: An old release ignores the new cutoff setting
- **WHEN** an old binary that predates `upload_task_creation_enabled` is considered for rollback
- **THEN** operators SHALL keep it outside upload initialization and lifecycle pools until the compatible task population has reached the documented terminal or frozen condition; presence of the environment variable SHALL NOT count as enforcement

#### Scenario: The cutoff rollout is incomplete
- **WHEN** any routed compatible API can still create a novel upload task, a rejected request changes `storage_reserved` or task count, or an existing task cannot resume through a compatible handler
- **THEN** rollback SHALL stop before any compatible handler is removed

### Requirement: Upload lifecycle rollback drain or freeze
After the new-task cutoff is closed and before an incompatible application release is admitted, operators SHALL close the public upload lifecycle ingress and obtain an auditable read-only snapshot that classifies active upload tasks as drained or frozen. The gateway SHALL reject every upload initialization, chunk, completion, and cancellation path with HTTP 503 and code `50013`; each declared compatible API SHALL report task creation disabled and zero in-flight business requests. The procedure SHALL NOT reverse `Finalizing`, clear or extend a lease, rewrite a staging descriptor, or claim that a full old-application rollback is safe.

#### Scenario: Active upload tasks are drained
- **WHEN** the frozen ingress is verified and the repeatable-read snapshot reports zero `InProgress` and zero `Finalizing` tasks
- **THEN** the upload task population MAY be recorded as drained while ingress remains closed, but this result SHALL NOT by itself admit an old release to other new-schema or new-blob routes

#### Scenario: Active upload tasks are frozen
- **WHEN** the frozen ingress is verified but the snapshot still contains `InProgress` or `Finalizing` tasks
- **THEN** the evidence SHALL digest their persisted identity, state, backend, prefix, version, owner, and lease deadline, SHALL require compatible handlers for later recovery, and SHALL keep the old release excluded from upload lifecycle routing

#### Scenario: A finalization lease remains during freeze
- **WHEN** a `Finalizing` task has an active or expired lease after compatible API in-flight requests reach zero
- **THEN** the procedure SHALL leave the row and lease unchanged so database time can expire it and a later compatible completion request can use the normal takeover CAS

#### Scenario: Upload ingress is reopened
- **WHEN** operators intend to leave freeze mode
- **THEN** they SHALL first restore and directly verify a compatible API pool and SHALL provide explicit unfreeze approval before applying the open gateway fragment

### Requirement: Contract migration observation gate
The data-to-contract gate SHALL begin only after incompatible processes have exited, production APIs create only S3 staging sessions, and legacy local non-terminal tasks, incomplete local cleanup jobs, and inventoried original-volume artifacts have reached zero. The observation SHALL span the configured upload expiry TTL and a distinct successful hourly expiration scan after that TTL, then complete a fresh four-scope reconciliation. Passing evidence SHALL authorize only review of a later contract migration and SHALL NOT execute destructive DDL or remove compatibility code.

#### Scenario: A post-cutover upload naturally expires and is reclaimed
- **WHEN** an S3 upload created after the recorded cutover time reaches its persisted `expires_at` according to PostgreSQL and a later hourly `expire_uploads` job succeeds
- **THEN** the task SHALL become Expired without direct database mutation, its unique cleanup job SHALL succeed, reserved quota and chunk rows SHALL be released, and its exact staging prefix SHALL become empty

#### Scenario: Service health and full reconciliation are checked after reclamation
- **WHEN** the expiration cleanup has converged
- **THEN** a newly created S3 upload SHALL still complete and download correctly, and a new scan ID SHALL finish every page of `contents`, `users`, `staging`, and `final` reconciliation with no unresolved finding or quota mismatch

#### Scenario: Contract readiness has a residual compatibility dependency
- **WHEN** any old process, pre-cutover active upload, local non-terminal task, incomplete cleanup, active upload lease, nullable field targeted for tightening, pending/retry/running/dead-letter upload job, reconciliation failure, or unresolved finding remains
- **THEN** the contract migration SHALL remain blocked and the expand schema and compatible handlers SHALL remain available

#### Scenario: The repository compresses the observation time
- **WHEN** an isolated integration test uses a shortened TTL and admits its claiming Worker only after PostgreSQL reports the probe expired
- **THEN** the Worker's real initial periodic seed MAY represent the later expiration scan, while production acceptance SHALL still require the actual configured TTL and a distinct subsequent hourly scan

### Requirement: Expand schema survives emergency application rollback
Emergency application rollback SHALL preserve every applied expand migration and SHALL NOT execute a `*_rollback.sql` file, remove a migration-ledger row, or perform contract DDL. Destructive migration reversal SHALL be a separate pre-activation schema change available only through the reviewed reversal entry point, which SHALL require an exact pre-activation context, explicit approval, a change-ticket identifier, and a SHA-256 readiness-evidence digest before opening a database session. Each destructive SQL file SHALL independently enforce the same inputs before its first DDL statement.

#### Scenario: An application rollback is urgent
- **WHEN** operators roll application processes or configuration back because a candidate release fails
- **THEN** the rollback evidence SHALL record `schema_action=preserve_expand` and `contract_migration_allowed=false`, and no database reversal command SHALL run

#### Scenario: A destructive SQL file is invoked directly
- **WHEN** a rollback SQL file is passed to `psql` without every reviewed pre-activation authorization input, or with `emergency_application_rollback` as its context
- **THEN** execution SHALL fail before DDL and SHALL preserve both the expand objects and their migration-ledger rows

#### Scenario: A separately approved pre-activation reversal is requested
- **WHEN** the new capability has not been activated, the approved readiness evidence proves every migration-specific data guard, and the exact version is submitted through the reviewed reversal entry point
- **THEN** the SQL MAY evaluate its existing data guards and execute transactionally, while any authorization or data-guard failure SHALL leave the schema unchanged

#### Scenario: A later contract migration is proposed
- **WHEN** the observation gate passes after production activation
- **THEN** the result SHALL authorize only a new reviewed contract change with its own DDL, restore rehearsal, owners, evidence, and approval; it SHALL NOT authorize reuse of a historical rollback SQL file

### Requirement: Upgrade and rollback operations
Deployment documentation SHALL define upgrade preparation, backup and isolated restore rehearsal, test-environment verification, build/deploy steps, database migration application, health validation, application/configuration/traffic rollback with expand preservation, and disaster recovery as a separate procedure.

#### Scenario: Upgrade fails validation
- **WHEN** a new deployment fails health or functional validation after upgrade
- **THEN** the operator SHALL have documented rollback steps for traffic isolation, service stop, restoration of a schema-compatible binary and configuration, service restart, and validation without reversing committed expand migrations
