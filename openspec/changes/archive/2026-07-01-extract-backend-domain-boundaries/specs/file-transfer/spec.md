## ADDED Requirements

### Requirement: Upload Lifecycle Boundary
The backend SHALL expose upload lifecycle behavior through an explicit domain boundary that preserves current upload initialization, chunk acceptance, completion, cancellation, expiry, quota, content, temporary-storage, and response semantics.

#### Scenario: Non-instant upload initialized
- **WHEN** an authenticated user initializes a valid non-instant upload with sufficient quota
- **THEN** the upload lifecycle SHALL reserve storage, create an in-progress upload task, return chunk instructions, and avoid creating file metadata until completion

#### Scenario: Instant upload initialized
- **WHEN** upload initialization finds existing content matching the requested file hash
- **THEN** the upload lifecycle SHALL create file metadata referencing the existing content, update content reference state safely, and preserve the current accounting behavior for reused physical content

#### Scenario: Upload completion succeeds
- **WHEN** an upload completes with all chunks present and matching integrity metadata
- **THEN** the upload lifecycle SHALL assemble and verify content, reuse or create content metadata, create file metadata, convert reserved storage to used storage atomically with database finalization, mark the task completed, clean chunk tracking, and clean temporary artifacts

#### Scenario: Upload completion fails after blob promotion
- **WHEN** final blob promotion succeeds but later database finalization fails
- **THEN** the upload lifecycle SHALL attempt explicit compensation for the promoted blob and SHALL report/log any orphan-risk condition

#### Scenario: Upload cancelled or expired
- **WHEN** an in-progress upload is cancelled by the user or expires by scheduled cleanup
- **THEN** the upload lifecycle SHALL release reserved storage, move the task to the appropriate terminal state, clean chunk tracking when applicable, and clean temporary artifacts

### Requirement: Upload Domain Dependencies
Upload lifecycle implementation SHALL coordinate content reference operations and storage accounting through explicit content and quota/accounting boundaries instead of duplicating direct `file_contents` or `users` mutations in each upload path.

#### Scenario: Upload path changes content references
- **WHEN** instant upload or upload completion reuses or creates content
- **THEN** content lookup, creation, and reference-count mutations SHALL flow through the content boundary while preserving transaction semantics

#### Scenario: Upload path changes quota accounting
- **WHEN** upload initialization, completion, cancellation, or expiry changes reserved or used storage
- **THEN** quota reservation, release, and reserved-to-used commit SHALL flow through the quota/accounting boundary while preserving atomic database consistency
