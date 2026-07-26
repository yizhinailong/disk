# backend-safety-net Specification

## Purpose

Define backend integration safety-net requirements that protect upload lifecycle, content reference counts, quota accounting, trash cleanup, and file/folder namespace behavior during refactors.

## Requirements

### Requirement: Upload lifecycle safety invariants
The system SHALL provide integration safety tests that verify upload lifecycle operations preserve current database and filesystem invariants, and cleanup-related upload safety scenarios MUST be runnable through a deterministic cleanup trigger that exercises the same implementation as production scheduled cleanup.

#### Scenario: Successful chunked upload records durable completion
- **WHEN** a test performs upload init, uploads all chunks, and completes the upload through the public API
- **THEN** the safety test MUST verify that the upload task is completed, reserved storage is released, used storage follows the current product rule, a `files` row exists, a `file_contents` row exists or is reused, and temporary upload artifacts are removed

#### Scenario: Canceled upload releases reservation and artifacts
- **WHEN** a test initializes an upload, writes at least one chunk, and cancels the upload before completion
- **THEN** the safety test MUST verify that reserved quota is released, no active `files` row is created for the canceled upload, and temporary upload artifacts are removed

#### Scenario: Expired upload cleanup releases reservation and artifacts
- **WHEN** a test prepares or observes an expired in-progress upload and runs the deterministic cleanup trigger for the existing expired-upload cleanup path
- **THEN** the safety test MUST verify that the upload task is marked expired or otherwise no longer active, reserved quota is released, no `files` row is created, and temporary upload artifacts are removed

#### Scenario: Expired upload cleanup is replayed
- **WHEN** the deterministic expired-upload cleanup is run again after one target upload has already reached Expired and its staging cleanup has succeeded
- **THEN** the replay MUST NOT transition that upload again, decrement reserved quota below its settled value, change its state version or finalization timestamp, enqueue another staging-cleanup job, create a file or content reference, or delete any final blob

### Requirement: Content reference-count safety invariants
The system SHALL provide integration safety tests that characterize current content deduplication and reference-count behavior, including upload completion races and permanent cleanup paths that may affect shared content.

#### Scenario: Instant upload reuses existing content
- **WHEN** a file content already exists and a second upload is initialized with the same content hash and a non-conflicting filename
- **THEN** the safety test MUST verify that instant upload is used, the logical file references the existing content row, and `file_contents.ref_count` changes according to the current backend behavior

#### Scenario: Completion deduplicates content that appears before finalize
- **WHEN** an upload task is initialized for content that does not yet exist and matching content appears before that task is completed
- **THEN** the safety test MUST verify that completion reuses or creates `file_contents` according to the current backend behavior and leaves reference counts consistent with the resulting logical files

#### Scenario: Copy operation preserves content reference accounting
- **WHEN** a test copies a file that references an existing content row
- **THEN** the safety test MUST verify that the copied file references the intended content and that `file_contents.ref_count` increments or is preserved according to the current backend behavior

#### Scenario: Permanent trash cleanup decrements content references safely
- **WHEN** trashed files are permanently deleted or expired-trash cleanup runs through the deterministic cleanup trigger
- **THEN** the safety test MUST verify that `file_contents.ref_count` is decremented according to the current backend behavior and physical blob deletion only occurs after the content has no remaining references

### Requirement: Quota accounting safety invariants
The system SHALL provide integration safety tests that characterize current quota reservation and storage accounting behavior, including deterministic cleanup execution for expired upload reservations and expired trash storage release.

#### Scenario: Upload init enforces used plus reserved quota
- **WHEN** a test attempts to initialize an upload where `storage_used + storage_reserved + file_size` exceeds `storage_quota`
- **THEN** the safety test MUST verify that the upload is rejected and no additional upload task or reservation is left behind

#### Scenario: Upload reservation commits on successful completion
- **WHEN** a chunked upload completes successfully
- **THEN** the safety test MUST verify that `users.storage_reserved` decreases and `users.storage_used` changes according to the current upload completion product rule

#### Scenario: Copy checks quota before creating logical file records
- **WHEN** a copy request would exceed available quota
- **THEN** the safety test MUST verify that the copy is rejected or copies zero files according to current behavior and that no extra logical file rows or content-reference increments are left behind

#### Scenario: Trash permanent deletion releases used storage according to current rule
- **WHEN** a file is moved to trash and then permanently deleted or removed by expired-trash cleanup through the deterministic cleanup trigger
- **THEN** the safety test MUST verify that `users.storage_used` changes according to the current backend rule and that the observed rule is documented by the test name or assertion message

### Requirement: File and folder namespace safety invariants
The system SHALL provide integration safety tests that characterize current move, copy, and path consistency behavior for files and folders.

#### Scenario: Moving a file updates parent and path
- **WHEN** a test moves a file from one folder to another through the public API
- **THEN** the safety test MUST verify that the file row has the new parent folder, the path matches the destination folder path and filename, and item counts follow current behavior

#### Scenario: Moving a folder updates subtree paths
- **WHEN** a test moves a folder that contains nested folders and files
- **THEN** the safety test MUST verify that the moved folder path, descendant folder paths, and descendant file paths are updated consistently

#### Scenario: Moving a folder into itself or descendant is rejected
- **WHEN** a test attempts to move a folder into itself or one of its descendant folders
- **THEN** the safety test MUST verify that the operation is rejected and the original folder and file paths remain unchanged

#### Scenario: Copying a folder preserves tree shape and content references
- **WHEN** a test copies a folder containing nested folders and files
- **THEN** the safety test MUST verify that the copied tree preserves the original shape under the target folder and that copied files reference content according to current backend behavior

### Requirement: Safety tests integrate with existing test execution
The system SHALL run backend safety-net tests through the existing integration-test execution contract, and cleanup safety tests MUST invoke cleanup deterministically without relying on production scheduler timing.

#### Scenario: CTest runs safety tests through uv
- **WHEN** CTest executes the backend integration test suite
- **THEN** each safety-net script MUST run through `uv run`, use the repository root as working directory, and be marked serial to avoid shared database or filesystem races

#### Scenario: Safety helpers use configured backend state
- **WHEN** a safety-net script needs database or storage information
- **THEN** it MUST resolve connection and storage settings from environment variables and repository configuration rather than hard-coded assumptions, while preserving sensible defaults for the current local setup

#### Scenario: Cleanup safety tests use deterministic cleanup seam
- **WHEN** a safety-net script needs to exercise expired upload or expired trash cleanup
- **THEN** it MUST trigger the existing cleanup implementation through a deterministic service/helper/manual seam and MUST NOT wait for the production scheduler to run

#### Scenario: Production scheduled cleanup remains unchanged
- **WHEN** production scheduled cleanup runs after this change
- **THEN** it MUST continue to call the same cleanup implementation and preserve existing cleanup cadence, batch limits, and operational behavior
