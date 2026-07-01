## Purpose

TBD.

## Requirements

### Requirement: Architecture decision records
The project SHALL preserve durable architecture decisions as OpenSpec-covered reference contracts, including decision status, context, decision, rationale, alternatives, impact, rollback or revisit conditions, and related documents.

#### Scenario: Decision is reviewed later
- **WHEN** a future reviewer revisits an architecture decision
- **THEN** the record SHALL show why the decision was accepted, what alternatives were rejected, what constraints apply, and when the decision should be reconsidered

### Requirement: PostgreSQL database-selection decision
The architecture documentation SHALL preserve the accepted decision to use PostgreSQL as the primary database and its constraints.

#### Scenario: PostgreSQL rationale requested
- **WHEN** a stakeholder asks why PostgreSQL is used
- **THEN** the architecture record SHALL cite recursive CTE behavior, JSONB support, concurrency control, type-system evolution, SQL standards support, Drogon ORM support, and operational trade-offs

#### Scenario: PostgreSQL decision revisit criteria reviewed
- **WHEN** repeated database-related regressions, unacceptable performance regression, blocking ORM issues, or unacceptable operations instability occur during the two-week observation window after database cutover
- **THEN** the architecture decision record SHALL identify those conditions as triggers to revisit the PostgreSQL decision

### Requirement: Download streaming API decision
The architecture documentation SHALL preserve the evaluation of Drogon `newStreamResponse`, `newAsyncStreamResponse`, and `newFileResponse` for file download behavior.

#### Scenario: Async stream replacement proposed
- **WHEN** a future change proposes replacing file downloads with `newAsyncStreamResponse`
- **THEN** the architecture record SHALL require review of chunked-only behavior, `Content-Length`, Range support, progress display, coroutine limitations, and performance overhead before accepting the change

#### Scenario: Current download paths documented
- **WHEN** download implementation architecture is reviewed
- **THEN** the architecture decision record SHALL identify large-file downloads as using the `newFileResponse`/sendfile path and small-file downloads as using the `newStreamResponse` path

### Requirement: io_uring feasibility decision
The architecture documentation SHALL preserve the No-Go decision for introducing io_uring at the current stage.

#### Scenario: io_uring reconsidered
- **WHEN** a future change proposes io_uring adoption
- **THEN** the record SHALL require reconsideration of upstream Drogon/Trantor support, database-driver compatibility, kernel requirements, cross-platform impact, expected workload, and measured performance benefit

#### Scenario: io_uring decision rationale reviewed
- **WHEN** performance optimization priorities are discussed in relation to io_uring
- **THEN** the architecture decision record SHALL capture why a custom io_uring fork is or is not justified for the measured workload

### Requirement: Architecture decisions are behavior-preserving until implemented by change
Architecture decision documentation SHALL NOT by itself alter runtime behavior; implementation changes SHALL be proposed and applied through separate OpenSpec changes when behavior or architecture actually changes.

#### Scenario: Decision identifies future opportunity
- **WHEN** an ADR or analysis identifies a possible future optimization such as real-time notifications or new streaming architecture
- **THEN** that opportunity SHALL remain informational until a separate change adds implementation requirements and tasks

### Requirement: Backend Domain Boundary Decision
The architecture documentation SHALL preserve the accepted decision to extract backend content, quota/accounting, upload lifecycle, and trash lifecycle boundaries incrementally while preserving current runtime behavior.

#### Scenario: Domain extraction approach reviewed
- **WHEN** a future reviewer evaluates backend domain extraction work
- **THEN** the architecture record SHALL show that content and quota/accounting primitives are extracted before upload and trash lifecycle orchestration

#### Scenario: Behavior change proposed during extraction
- **WHEN** a domain-boundary refactor would alter quota semantics, trash accounting, upload responses, upload task status meanings, or blob deletion behavior
- **THEN** the change SHALL be treated as a separate behavior change rather than a silent part of the extraction

### Requirement: Explicit Blob Deletion Safety Decision
The architecture documentation SHALL preserve the decision that physical blob deletion remains guarded by explicit zero-reference verification rather than being hidden behind generic ref-count helpers.

#### Scenario: Blob deletion helper is introduced
- **WHEN** a service or helper proposes deleting final content blobs after reference-count updates
- **THEN** the design SHALL require a fresh zero-reference verification before deletion and SHALL keep the storage deletion step visible in lifecycle code or an explicitly named cleanup primitive
