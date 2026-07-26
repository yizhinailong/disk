## Purpose

Defines reproducible unit, integration, system, security, compatibility, and performance validation evidence and reporting contracts.

## Requirements

### Requirement: Multi-level validation coverage
The system SHALL define validation requirements across unit tests, integration tests, system tests, desktop documentation checks, performance tests, security tests, and compatibility tests.

#### Scenario: Validation plan reviewed
- **WHEN** a release or documentation set is reviewed
- **THEN** the reviewer SHALL be able to identify which validation levels apply and what evidence is expected for each level

### Requirement: Backend unit and integration test coverage
The validation documentation SHALL enumerate backend test coverage for DTO validation, password hashing, token services, Redis services, authentication filters, file/share/trash services, cleanup, system information, operation logs, upload consistency, and supporting utilities.

#### Scenario: Backend test inventory checked
- **WHEN** a developer checks backend validation readiness
- **THEN** the documentation SHALL identify the relevant test files or test categories and the functional areas they cover

#### Scenario: Test command needed
- **WHEN** a developer needs to run backend tests
- **THEN** the documentation SHALL provide CMake/CTest or executable-level commands for full and focused test execution

### Requirement: Distributed multi-instance integration validation
The backend test suite SHALL provide serial, environment-gated CTest entries for the authoritative Compose topology and for an equivalent local-process runner when Docker is unavailable. Both entries SHALL execute the same distributed-flow assertions against two API processes, two Worker processes, one shared PostgreSQL database, one password-protected persistent Redis-compatible service, one shared MinIO service, and a non-sticky load-balancing endpoint.

#### Scenario: Compose distributed flow is selected
- **WHEN** `DISK_DISTRIBUTED_INTEGRATION=1` selects the Compose entry
- **THEN** the test SHALL execute the complete cross-instance and dependency-failure flow against `docker-compose.distributed.yml`

#### Scenario: Local-process distributed flow is selected
- **WHEN** Docker is unavailable and `DISK_DISTRIBUTED_LOCAL_INTEGRATION=1` selects the local entry with an executable `DISK_MINIO_BIN`
- **THEN** the runner SHALL start isolated real dependencies and four application processes, execute the unchanged distributed-flow assertions, preserve evidence, and tear down its temporary topology

#### Scenario: Fixed MinIO test dependencies are prepared
- **WHEN** a Linux amd64 developer explicitly asks the repository helper to prepare the reviewed MinIO server and client in a chosen output directory
- **THEN** the helper SHALL fetch only the pinned official HTTPS archive URLs, verify each hard-coded SHA-256 before making or atomically publishing an executable, reuse only an existing file with the exact reviewed digest, and reject an existing mismatch without overwriting it

#### Scenario: Distributed flow gate is not selected
- **WHEN** either distributed entry is invoked without its required environment gate
- **THEN** it SHALL report an environment-gated skip that SHALL NOT count as distributed acceptance evidence

#### Scenario: Upload terminal contenders race across API instances
- **WHEN** completion, cancellation, and expiration scanning concurrently contend for the same active or database-expired upload through different API instances
- **THEN** the task SHALL reach exactly one legal terminal state, clear its lease and chunk rows, settle reserved and used quota exactly once, preserve file/content/ref-count invariants, and enqueue exactly one deduplicated staging-cleanup task

#### Scenario: Shared-user reservations change during a fault wait
- **WHEN** a lease-expiry fault scenario waits long enough for an unrelated upload owned by the same test user to expire or settle
- **THEN** validation SHALL prove the target task's single settlement from its terminal state and persisted accounting fields, and in one database snapshot SHALL calculate the residual between current reserved quota and all Pending and Finalizing task reservations and prove that residual is unchanged from the pre-fault snapshot instead of comparing stale aggregate totals or attributing a pre-existing shared-fixture residual to the target task

### Requirement: Data consistency audit validation
Automated validation SHALL audit exact file counts, user used and reserved quota, content reference counts, bidirectional database/object existence in the staging and final namespaces, and provider-visible incomplete multipart uploads.

#### Scenario: Restored data is audited and repaired
- **WHEN** a coordinated database and final-object recovery set is restored into isolated resources
- **THEN** a real Worker SHALL complete every page of the `contents`, `users`, `staging`, and `final` scopes, preserve the expected file count, reject injected quota, reference-count, missing-object, size, and staging/final orphan faults with exact persisted findings, and resolve those findings only after repair and a complete new scan

#### Scenario: Multipart storage is audited
- **WHEN** a multipart-capable S3 upload is assembled, promoted, and cleaned or a persisted multipart-abort task is recovered
- **THEN** evidence SHALL use the provider multipart inventory to prove incomplete uploads converge to zero while file, content, quota, final-object hash, and staging-object invariants remain correct

### Requirement: System functional validation
The system test plan SHALL define functional test cases for authentication, user profile and storage, file upload/download, file and folder management, trash lifecycle, sharing, and system/admin interfaces.

#### Scenario: Functional test case executed
- **WHEN** a system test case is executed
- **THEN** it SHALL specify the operation, setup or steps, expected result, and priority

### Requirement: Security validation
The validation documentation SHALL include security checks for authentication, authorization, input validation, file safety, S3 object-key boundaries, S3 endpoint parsing, credential-safe evidence, TLS/transport behavior, replay, and rate limiting.

#### Scenario: Authorization security tested
- **WHEN** tests attempt cross-user file, folder, or share access
- **THEN** the expected result SHALL be rejection rather than unauthorized access

#### Scenario: Cross-user upload lifecycle is tested
- **WHEN** a second authenticated user attempts chunk, completion, cancellation, and replay operations with another user's `upload_id`
- **THEN** evidence SHALL show a uniform not-found response and no task, lease, chunk, staging, file, or quota mutation

#### Scenario: S3 input boundaries are tested
- **WHEN** tests inject traversal or delimiter forms into object locators and non-origin URL components into S3 endpoint configuration
- **THEN** unsafe values SHALL be rejected before storage calls or S3 client initialization, and cleanup SHALL remain inside the exact upload-session prefix

#### Scenario: Credential-safe evidence is tested
- **WHEN** structured and plain-text fixtures containing nested tokens, authentication headers, passwords, environment credentials, and JWT-shaped values are saved through the shared evidence helper
- **THEN** saved evidence SHALL redact replayable credentials while retaining non-secret diagnostic identifiers

#### Scenario: Rate limit tested
- **WHEN** requests exceed documented login, API, or share-access limits
- **THEN** the system SHALL reject or throttle according to the configured limit behavior

### Requirement: Compatibility validation
The validation documentation SHALL define compatibility checks for supported network environments, operating systems, database/cache versions, and client integration paths where the source documents define compatibility expectations.

#### Scenario: Network compatibility tested
- **WHEN** compatibility validation covers network conditions
- **THEN** the tests SHALL include normal wired or Wi-Fi use, weak-network transfer behavior, and network interruption/recovery expectations

#### Scenario: Platform compatibility reviewed
- **WHEN** compatibility validation covers supported platforms
- **THEN** the tests SHALL identify which Linux and Windows environments are expected to build, run, or remain compatible

### Requirement: Performance and pressure validation
The validation documentation SHALL define performance objectives and pressure-test procedures for health checks, login, file listing, upload initialization, file upload, file download, and mixed workload scenarios.

#### Scenario: Performance target reviewed
- **WHEN** backend performance readiness is reviewed
- **THEN** documented targets SHALL include concurrent user or transfer expectations, API response-time goals, QPS or throughput goals, upload/download throughput expectations, availability goals, and acceptable error-rate thresholds where source documents define them

#### Scenario: API pressure test executed
- **WHEN** a pressure test is run against a documented endpoint
- **THEN** the test SHALL record request count, concurrency, duration or thread settings, QPS or throughput, average latency, and error rate

#### Scenario: Distributed baseline comparison executed
- **WHEN** the post-refactor API scaling, storage workload, S3 assembly, process-failure, and Worker-backlog results are compared with their recorded baselines
- **THEN** the comparison SHALL reject missing or mismatched scenarios, parameters, required host fields, dependency implementations, dirty or inconsistent current commits or server binaries, or failed source acceptance; otherwise it SHALL preserve source hashes, commits, host load, directional metric deltas, and material observations in schema-versioned evidence

#### Scenario: Tool limitation disclosed
- **WHEN** `drogon_ctl press` is used for pressure testing
- **THEN** the documentation SHALL disclose limitations such as lack of percentile latency, single-step request shape, static request bodies, or empty-database bias where applicable

### Requirement: Desktop documentation validation
The validation documentation SHALL provide docs-only checks for desktop documentation structure, terminology, cross-references, ID traceability, implementation anchors, status labels, diff scope, and legacy reference handling.

#### Scenario: Desktop docs verified
- **WHEN** desktop documentation validation is executed
- **THEN** the evidence SHALL show whether required documents exist, references resolve, status labels are defensible, source/reference status is clear, and changes remain within the intended documentation scope

### Requirement: Evidence and reporting discipline
Validation activities SHALL produce or reference evidence sufficient to reproduce commands, outcomes, and failure severity.

#### Scenario: Validation evidence captured
- **WHEN** a validation command is run as part of a documented plan
- **THEN** the evidence SHALL include the command, timestamp or context, exit result, and relevant output or report summary

#### Scenario: Evidence contains runtime data
- **WHEN** a validation helper persists API responses, headers, configuration fragments, or diagnostic text
- **THEN** it SHALL use the shared redacting evidence path and SHALL NOT persist replayable credentials; raw-byte evidence SHALL be limited to synthetic test fixtures

#### Scenario: Defect severity assigned
- **WHEN** validation finds a failure
- **THEN** the failure SHALL be classified using documented severity or priority criteria so blockers are distinguishable from follow-up improvements

### Requirement: Feature Consistency Validation
The validation documentation SHALL include coverage for P2 feature consistency behavior across Web admin storage editing, Web folder tree store integration, Desktop visitor resumed downloads, download integrity verification, and Web large-file memory pressure.

#### Scenario: Admin storage edit validation executed
- **WHEN** validation covers Web administrator user management
- **THEN** it SHALL verify successful quota update, invalid quota rejection, non-administrator rejection, and UI refresh/error feedback

#### Scenario: Folder tree state validation executed
- **WHEN** validation covers Web file navigation
- **THEN** it SHALL verify that folder tree selection, current folder, file list, breadcrumb, and refresh behavior remain consistent after tree selection, breadcrumb navigation, file-list drill-down, and folder mutation

#### Scenario: Desktop visitor resume validation executed
- **WHEN** validation covers Desktop visitor downloads under interruption and recovery conditions
- **THEN** it SHALL verify that the client resumes with Range requests when safe and restarts or fails explicitly when partial state cannot be trusted

#### Scenario: Download integrity validation executed
- **WHEN** validation covers completed downloads
- **THEN** it SHALL verify expected-size checks, available hash or checksum checks, mismatch failure handling, and retry or restart behavior

#### Scenario: Web large-file pressure validation executed
- **WHEN** validation covers Web large-file downloads
- **THEN** it SHALL record file size, browser/runtime context, memory behavior or proxy indicators, completion result, and error handling evidence showing that large payloads are not stored in centralized UI state

### Requirement: Backend Domain Extraction Validation
Validation documentation SHALL include characterization and regression coverage for backend content, quota/accounting, upload lifecycle, and trash lifecycle extraction.

#### Scenario: Upload lifecycle validation executed
- **WHEN** validation covers upload domain extraction
- **THEN** it SHALL verify normal init/chunk/complete, instant upload, finalize-time deduplication, cancellation, expiry cleanup, task terminal states, temporary cleanup, and unchanged API response envelopes

#### Scenario: Content reference validation executed
- **WHEN** validation covers content-domain extraction
- **THEN** it SHALL verify lookup/reuse, content creation, ref-count increments from upload/copy, ref-count decrements from permanent trash deletion, and zero-reference blob cleanup safety

#### Scenario: Quota accounting validation executed
- **WHEN** validation covers quota/accounting extraction
- **THEN** it SHALL verify reservation, insufficient quota rejection, reserved release, reserved-to-used commit, used-storage release on permanent trash deletion, and preservation of current instant-upload and trash accounting behavior

#### Scenario: Failure-domain validation executed
- **WHEN** validation covers DB and filesystem side effects around upload finalization or trash cleanup
- **THEN** it SHALL verify documented compensation or safe-skip behavior for promoted blobs, temporary artifacts, database rollback, and blob deletion failures

### Requirement: Share Operation Rate Limit Validation
Automated validation SHALL prove the configured boundaries, route coverage,
identity and operation isolation, authentication precedence, response contract,
fixed-window behavior, Redis failure policy, and credential exclusion for share
access, browse, and download rate limits.

#### Scenario: SHARE-RATE-ACCESS-001 access boundary is validated
- **WHEN** the share rate-limit integration scenario exercises an active no-password share from one normalized IP
- **THEN** evidence SHALL show that the configured number of access requests continue and the next request returns HTTP 429 with code `10005`

#### Scenario: SHARE-RATE-BROWSE-001 browse boundary is validated
- **WHEN** the scenario exercises authenticated browse with one verified JTI
- **THEN** evidence SHALL show that the configured number of browse requests continue and the next request is throttled

#### Scenario: SHARE-RATE-DOWNLOAD-001 shared download boundary is validated
- **WHEN** the scenario mixes download metadata, binary content, and save-to-drive for one JTI
- **THEN** evidence SHALL show that all covered routes consume one configured download bucket and the next request through any covered route is throttled

#### Scenario: SHARE-RATE-RANGE-001 request charging is validated
- **WHEN** the scenario sends Range resume and retry HTTP requests
- **THEN** evidence SHALL show that each request consumes the download bucket exactly once

#### Scenario: SHARE-RATE-ISOLATION-001 isolation is validated
- **WHEN** the scenario uses multiple operation families and separately issued token JTIs
- **THEN** evidence SHALL show that operation families and distinct JTIs do not consume one another's counters

#### Scenario: SHARE-RATE-AUTH-001 authentication precedence is validated
- **WHEN** the scenario sends missing, invalid, revoked, or insufficient-scope tokens
- **THEN** evidence SHALL show the existing authentication or authorization responses and no authenticated counter consumption

#### Scenario: SHARE-RATE-CONFIG-001 configuration is validated
- **WHEN** focused tests load valid, absent, zero, and negative share operation settings
- **THEN** evidence SHALL show configured values or the documented per-family defaults as applicable

#### Scenario: SHARE-RATE-RESPONSE-001 limited responses are validated
- **WHEN** each share limiter crosses its configured boundary
- **THEN** evidence SHALL show HTTP 429, code `10005`, and all four standard rate-limit headers

#### Scenario: SHARE-RATE-REDIS-001 fail-open behavior is validated
- **WHEN** focused tests make Redis limiter accounting fail
- **THEN** evidence SHALL show observable non-secret failure diagnostics and continuation to the underlying business response

#### Scenario: SHARE-RATE-SECRETS-001 credential exclusion is validated
- **WHEN** the scenario inspects Redis keys, logs, audit rows, fixtures, and saved evidence produced by share limiting
- **THEN** evidence SHALL show no raw Share Token, authentication header, password, or password hash

#### Scenario: Share rate-limit scenario is part of the backend suite
- **WHEN** the standard backend CTest suite is configured
- **THEN** the share operation integration scenario SHALL be registered as a serial test and SHALL use the shared backend lifecycle harness
