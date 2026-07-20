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
Deployment documentation SHALL define safe forward migration, reconciliation checks, rollback scripts, stop conditions, and backup restoration for documented database migrations.

#### Scenario: Migration starts
- **WHEN** an operator starts a database migration
- **THEN** the procedure SHALL require a completed backup before forward SQL is applied

#### Scenario: Reconciliation fails
- **WHEN** post-change reconciliation SQL reports missing data, inconsistent reference counts, or unexpected values
- **THEN** the procedure SHALL require rollback or backup restoration before continuing the application release

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
The final Blob copy command SHALL default to a non-writing dry run and SHALL verify each local source and each existing or newly uploaded target by size, MD5, and SHA-256. Execution SHALL commit a durable, exclusively locked per-object checkpoint only after a complete target GET passes, and SHALL bind that checkpoint to the exact manifest SHA-256, bucket, and object prefix. A positive transfer-rate setting SHALL apply one aggregate byte budget to S3 upload callbacks and complete target GET verification; zero SHALL disable the limit and a negative setting SHALL be rejected.

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
Deployment documentation SHALL define HTTPS certificate options, reverse proxy settings, upload body-size limits tied to chunk size, timeout requirements, and download buffering considerations.

#### Scenario: Nginx front end configured
- **WHEN** Nginx proxies requests to the backend
- **THEN** the configuration SHALL preserve host and forwarding headers, support long upload/download operations, and set request body limits based on upload chunk size rather than maximum logical file size

### Requirement: Monitoring, logging, backup, restore, and troubleshooting
Deployment documentation SHALL define log rotation, health checks, service/database/cache monitoring, database and file backups, restore procedures, and troubleshooting paths for startup, database, Redis, upload, CPU, memory, and disk I/O issues. If the Prometheus plugin is enabled, deployment documentation SHALL identify the `/metrics` endpoint as the metrics scrape hook.

#### Scenario: Routine health check
- **WHEN** an operator verifies a running deployment
- **THEN** they SHALL be able to check service status, listening port, disk usage, PostgreSQL connectivity, Redis connectivity, and `/api/health`

#### Scenario: Scheduled backup configured
- **WHEN** a production deployment is prepared
- **THEN** database and file backups SHALL have documented commands, retention expectations, and restore steps

#### Scenario: Isolated restore drill completed
- **WHEN** backup readiness is accepted for a release
- **THEN** a coordinated database and final-object recovery set SHALL be restored into isolated empty resources and SHALL pass schema, quota, reference-count, object-integrity, paginated reconciliation, failure-blocking, and repaired-rescan checks before traffic is allowed

### Requirement: Least-privilege object-storage provisioning
Distributed deployment artifacts SHALL provision distinct staging and final key namespaces, a lifecycle policy that cannot expire final objects, and an application identity limited to required data-plane operations in those namespaces. Object-store root or administrative credentials SHALL be confined to provisioning and SHALL NOT be injected into API or Worker processes. Application S3 dependency failures and reconciliation findings SHALL be monitored, while provider-native availability, capacity, replication, healing, and lifecycle monitoring remain required for the target object store.

#### Scenario: MinIO sample is provisioned
- **WHEN** the repository MinIO initializer runs with separate root and application credentials
- **THEN** it SHALL create the bucket, import and verify lifecycle rules, idempotently provision and bind the application policy, and leave API and Worker processes configured only with the application credentials

#### Scenario: Application S3 credentials are exercised
- **WHEN** the application identity accesses the sample bucket
- **THEN** required list, object, copy-compatible, delete, and multipart operations under `objects/*` and `staging/*` SHALL succeed while access outside those namespaces and bucket administration SHALL be denied

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

#### Scenario: S3 staging activation fails its gate
- **WHEN** a new task persists the wrong backend or prefix, an S3 task creates node-local staging data, required S3 operations fail, or an existing task descriptor changes
- **THEN** the rollout SHALL stop and MAY restore local staging for subsequently created tasks, but SHALL keep a compatible release available to finish or explicitly cancel already-created S3 tasks

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

### Requirement: Upgrade and rollback operations
Deployment documentation SHALL define upgrade preparation, backup, test-environment verification, build/deploy steps, database migration application, health validation, and rollback to a previous application/database state.

#### Scenario: Upgrade fails validation
- **WHEN** a new deployment fails health or functional validation after upgrade
- **THEN** the operator SHALL have documented rollback steps for service stop, previous binary/config restoration, database restore if needed, and service restart
