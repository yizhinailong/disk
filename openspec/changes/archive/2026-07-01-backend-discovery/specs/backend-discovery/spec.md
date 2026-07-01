## ADDED Requirements

### Requirement: Discovery artifacts capture current backend behavior
The system SHALL provide backend discovery artifacts that document current consistency-sensitive backend behavior before behavior-preserving refactors depend on it.

#### Scenario: Upload lifecycle is documented
- **WHEN** maintainers inspect the backend discovery artifacts
- **THEN** they SHALL find the current upload init, chunk, complete, cancel, resume, and expire transitions, including upload task status changes and temporary storage cleanup side effects.

#### Scenario: Content lifecycle is documented
- **WHEN** maintainers inspect the backend discovery artifacts
- **THEN** they SHALL find the current `file_contents` create, reuse, ref-count increment, ref-count decrement, zero-ref verification, and physical blob deletion behavior.

#### Scenario: Quota lifecycle is documented
- **WHEN** maintainers inspect the backend discovery artifacts
- **THEN** they SHALL find the current `users.storage_reserved` and `users.storage_used` update paths, including upload reservation, upload finalization, upload cancel/expire, copy accounting, and trash expiration effects.

#### Scenario: Trash lifecycle is documented
- **WHEN** maintainers inspect the backend discovery artifacts
- **THEN** they SHALL find the current move-to-trash, retained content reference, expiration, purge, quota decrease, and blob cleanup behavior.

### Requirement: Filter and rate-limit behavior is verified
The system SHALL verify and document current filter and rate-limit behavior for representative protected and public backend routes.

#### Scenario: Protected route filter behavior is captured
- **WHEN** discovery verification is performed for protected file, upload, download, admin, and folder routes
- **THEN** the artifacts SHALL record which global and route-level filters execute, their observed order where practical, and whether JWT authentication executes more than once.

#### Scenario: Public route exemptions are captured
- **WHEN** discovery verification is performed for auth, health, and public share routes
- **THEN** the artifacts SHALL record whether those routes remain reachable without JWT and which public rate-limit filters still apply.

#### Scenario: Rate-limit Redis failure policy is captured
- **WHEN** discovery artifacts describe rate-limit filters
- **THEN** they SHALL state the current behavior when Redis increment operations fail for upload, download, register, admin, folder, and public share rate limits.

### Requirement: Behavior-preserving constraints are explicit
The system SHALL distinguish confirmed current behavior from recommendations or future behavior changes.

#### Scenario: Current behavior is separated from decisions
- **WHEN** a discovered behavior appears inconsistent or undesirable
- **THEN** the discovery artifacts SHALL record it as current behavior and list any proposed semantic change as an open question or follow-up, not as part of this change.

#### Scenario: Later refactor boundaries can cite constraints
- **WHEN** later refactor work extracts services or repositories
- **THEN** maintainers SHALL be able to identify the behavior-preserving constraints relevant to ContentService, QuotaService, UploadLifecycleService, TrashService, filter cleanup, and download-side-effect handling.

### Requirement: Evidence is traceable
The system SHALL make discovery conclusions traceable to source code, tests, runtime observations, or commands.

#### Scenario: Static findings cite source locations
- **WHEN** a discovery conclusion is based on source inspection
- **THEN** the artifacts SHALL cite the relevant file paths and line numbers or clearly named symbols.

#### Scenario: Runtime findings cite verification method
- **WHEN** a discovery conclusion depends on runtime behavior
- **THEN** the artifacts SHALL cite the test, command, log output, or observation method used to confirm it.
