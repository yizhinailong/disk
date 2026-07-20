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

### Requirement: Upgrade and rollback operations
Deployment documentation SHALL define upgrade preparation, backup, test-environment verification, build/deploy steps, database migration application, health validation, and rollback to a previous application/database state.

#### Scenario: Upgrade fails validation
- **WHEN** a new deployment fails health or functional validation after upgrade
- **THEN** the operator SHALL have documented rollback steps for service stop, previous binary/config restoration, database restore if needed, and service restart
