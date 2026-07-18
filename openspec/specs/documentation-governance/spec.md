## Purpose

Defines OpenSpec authority, reference-source status, evidence and traceability rules, and current-versus-archived documentation lifecycle governance.

## Requirements

### Requirement: OpenSpec as future requirement authority
The system SHALL treat OpenSpec capability specs as the primary authority for requirement changes in the migrated documentation areas.

#### Scenario: Future requirement change
- **WHEN** a future change affects desktop UX, documentation governance, persistence design, validation/performance, deployment operations, or architecture decisions
- **THEN** the change SHALL be proposed against the relevant OpenSpec capability before legacy narrative documents are updated as references

### Requirement: Legacy documentation status
The documentation governance SHALL identify legacy design, desktop, thesis, repository README, and client README documents as reference or historical sources unless a future change explicitly promotes content into OpenSpec.

#### Scenario: Legacy document consulted
- **WHEN** a reviewer uses a legacy document to understand background or detailed source material
- **THEN** the reviewer SHALL treat OpenSpec as the normative contract and the legacy document as supporting context only

#### Scenario: README consulted
- **WHEN** a reviewer uses `README.md` or a client README such as `clients/disk-tui/README.md`
- **THEN** the reviewer SHALL treat it as project overview or client integration context and SHALL resolve requirement-level changes through OpenSpec first

#### Scenario: Conflict between OpenSpec and reference source
- **WHEN** OpenSpec and a legacy or reference source document conflict
- **THEN** OpenSpec SHALL be used for requirement decisions and the conflict SHALL be resolved through a follow-up OpenSpec change

### Requirement: Behavior-preserving documentation governance
Documentation governance SHALL distinguish requirement documentation changes from runtime implementation changes and SHALL NOT imply application code, API, database, deployment, or runtime behavior changes unless a separate implementation change explicitly requires them.

#### Scenario: Runtime mismatch found in documentation review
- **WHEN** documentation review identifies a mismatch between a reference source and implementation
- **THEN** the mismatch SHALL be recorded as a follow-up concern rather than silently changing runtime behavior through documentation edits

### Requirement: Capability-oriented documentation structure
Requirement documentation SHALL be organized by durable capabilities rather than by the previous standalone Markdown document hierarchy.

#### Scenario: Source document contains multiple concerns
- **WHEN** a source document mixes UX, validation, persistence, deployment, or architecture material
- **THEN** its stable requirements SHALL be summarized into the relevant capability spec instead of copying the full document structure

#### Scenario: Detail is historical or overly specific
- **WHEN** source material is a historical note, implementation diary, exhaustive matrix, or non-normative prose
- **THEN** it SHALL remain as reference material unless it defines a stable, testable requirement

### Requirement: Status labels and evidence discipline
Desktop documentation SHALL preserve evidence-status discipline for verified runtime behavior, future intended behavior, and unverified code anchors.

#### Scenario: Capability claim lacks evidence
- **WHEN** a desktop documentation claim lacks implementation or verification evidence
- **THEN** the documentation SHALL avoid presenting it as verified runtime behavior and SHALL preserve future-intended or unverified status as appropriate

### Requirement: Traceable source coverage
Documentation governance SHALL preserve traceability from active OpenSpec requirements to relevant source areas such as `docs/design/`, `docs/desktop/`, selected `docs/lunwen/`, README files, and client README files when those sources are used as reference sources.

#### Scenario: Reviewer checks requirement source context
- **WHEN** a reviewer inspects a migrated or derived OpenSpec requirement
- **THEN** they SHALL be able to identify the relevant documentation source areas and any follow-up archival or reconciliation work that remains

### Requirement: Backend refactor decision documentation
Documentation governance SHALL keep backend refactor decisions traceable to their current status, the completed historical roadmap, and any verified active follow-up work. OpenSpec SHALL remain the normative authority for migrated requirements, while decision, backlog, archive, discovery, and design sources SHALL retain the roles defined by the scenarios below.

#### Scenario: Backend decision note is consulted
- **WHEN** a maintainer reviews `docs/backend-refactor-decisions.md`
- **THEN** the documentation SHALL distinguish accepted decisions, current implementation status, and any remaining follow-up work, SHALL trace completed roadmap history to `docs/archive/2026-07-14-backend-refactor-todo.md`, and SHALL trace only verified current unfinished work to `docs/TODO.md`

#### Scenario: Completed backend roadmap is consulted
- **WHEN** a maintainer needs the completed backend refactor sequence or its historical checklist
- **THEN** documentation SHALL identify `docs/archive/2026-07-14-backend-refactor-todo.md` as the historical completion record and SHALL NOT describe that roadmap as active work

#### Scenario: Current backend backlog references a decision
- **WHEN** `docs/TODO.md` lists verified unfinished backend work related to an accepted decision
- **THEN** the backlog SHALL retain only the current open work and SHALL link the relevant OpenSpec requirement, `docs/design/` source, or decision record without recreating completed roadmap tasks as active items

#### Scenario: Discovery and current decision status disagree
- **WHEN** `docs/backend-discovery.md` describes behavior that differs from the current status in `docs/backend-refactor-decisions.md`
- **THEN** documentation SHALL identify discovery as a historical observation, use the decision note for current decision and implementation status, and use OpenSpec as the normative requirement authority
