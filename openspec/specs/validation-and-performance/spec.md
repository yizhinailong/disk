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

### Requirement: System functional validation
The system test plan SHALL define functional test cases for authentication, user profile and storage, file upload/download, file and folder management, trash lifecycle, sharing, and system/admin interfaces.

#### Scenario: Functional test case executed
- **WHEN** a system test case is executed
- **THEN** it SHALL specify the operation, setup or steps, expected result, and priority

### Requirement: Security validation
The validation documentation SHALL include security checks for authentication, authorization, input validation, file safety, TLS/transport behavior, and rate limiting.

#### Scenario: Authorization security tested
- **WHEN** tests attempt cross-user file, folder, or share access
- **THEN** the expected result SHALL be rejection rather than unauthorized access

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
