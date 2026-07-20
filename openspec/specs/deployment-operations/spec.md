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
- **WHEN** API or Worker replicas are increased
- **THEN** the operator SHALL recalculate aggregate PostgreSQL, Redis, S3 connection, S3 I/O, and CPU budgets before rollout and SHALL retain the documented operational reserve

#### Scenario: Capacity moves beyond the measured matrix
- **WHEN** Worker replicas exceed 4, a local pool or concurrency limit is raised, or the production object-store implementation differs from the recorded baseline
- **THEN** a representative capacity test SHALL be completed before the new value is adopted as a production recommendation

### Requirement: Incremental database migration procedure
Deployment documentation SHALL define safe forward migration, reconciliation checks, rollback scripts, stop conditions, and backup restoration for documented database migrations.

#### Scenario: Migration starts
- **WHEN** an operator starts a database migration
- **THEN** the procedure SHALL require a completed backup before forward SQL is applied

#### Scenario: Reconciliation fails
- **WHEN** post-change reconciliation SQL reports missing data, inconsistent reference counts, or unexpected values
- **THEN** the procedure SHALL require rollback or backup restoration before continuing the application release

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

### Requirement: Upgrade and rollback operations
Deployment documentation SHALL define upgrade preparation, backup, test-environment verification, build/deploy steps, database migration application, health validation, and rollback to a previous application/database state.

#### Scenario: Upgrade fails validation
- **WHEN** a new deployment fails health or functional validation after upgrade
- **THEN** the operator SHALL have documented rollback steps for service stop, previous binary/config restoration, database restore if needed, and service restart
