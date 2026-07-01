## ADDED Requirements

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
