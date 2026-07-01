## 1. Test Helper Foundation

- [x] 1.1 Add a PostgreSQL integration helper in `test/integration/lib_py/db.py` that resolves connection settings from environment variables first and `config.json` second.
- [x] 1.2 Export DB helper functions from `test/integration/lib_py/__init__.py` for query, query-one, execute, and scalar reads.
- [x] 1.3 Add storage/config helper utilities for resolving `storage_base_path`, `temp_upload_path`, chunk size, and final blob/temp upload artifact paths.
- [x] 1.4 Add small assertion helpers for DB row existence, before/after numeric deltas, and filesystem presence/absence.

## 2. Upload Lifecycle Safety Tests

- [x] 2.1 Add `test/integration/test_safety_upload_invariants.py` with PEP 723 metadata including HTTP and PostgreSQL dependencies.
- [x] 2.2 Implement a successful chunked upload test that asserts `upload_tasks.status`, `users.storage_reserved`, `users.storage_used`, `files`, `file_contents`, and temp artifact cleanup.
- [x] 2.3 Implement a cancel-before-complete test that asserts reservation release, canceled task state, absence of a logical file row, and temp artifact cleanup.
- [x] 2.4 Implement or document the stable trigger for expired upload cleanup, then add the expired upload invariant test if the trigger is available.
- [x] 2.5 Ensure upload safety tests use unique filenames/content and clean up or isolate created test data.

## 3. Content Reference and Quota Safety Tests

- [x] 3.1 Add `test/integration/test_safety_content_quota.py` with shared upload helpers for creating known unique content.
- [x] 3.2 Add an instant-upload dedup test that verifies reused `file_contents` row and current `ref_count` behavior.
- [x] 3.3 Add a completion dedup test for the case where matching content appears after init but before finalize, or explicitly document why the current API/storage flow cannot make this deterministic.
- [x] 3.4 Add a copy reference-accounting test that verifies copied files use the intended `content_id` and checks current `ref_count` behavior.
- [x] 3.5 Add a quota rejection test for upload init where `storage_used + storage_reserved + file_size > storage_quota`, verifying no leaked reservation/task.
- [x] 3.6 Add a copy quota test that verifies over-quota copy leaves no extra logical files or content reference increments.
- [x] 3.7 Add a permanent trash deletion or expired-trash cleanup test that verifies `storage_used`, `ref_count`, and blob-retention/deletion behavior according to current backend rules.

## 4. Move, Copy, and Path Safety Tests

- [x] 4.1 Add `test/integration/test_safety_move_copy_path.py` for namespace and path invariants.
- [x] 4.2 Add a move-file test that verifies `folder_id`, path, and item-count behavior after moving a file between folders.
- [x] 4.3 Add a move-folder-subtree test that verifies moved folder path, descendant folder paths, and descendant file paths.
- [x] 4.4 Add rejection tests for moving a folder into itself and into a descendant, verifying original paths remain unchanged.
- [x] 4.5 Add a copy-folder-tree test that verifies copied tree shape and copied file content-reference behavior.

## 5. CTest Registration and Verification

- [x] 5.1 Register all new safety-net integration scripts in `test/CMakeLists.txt` with `uv run`, `RUN_SERIAL TRUE`, repository-root working directory, and suitable timeouts.
- [x] 5.2 Run the helper smoke path or targeted `uv run` commands for each new safety-net script in an environment with server, PostgreSQL, and Redis available. _(Skipped by user instruction because Redis is unavailable in this environment.)_
- [x] 5.3 Run targeted CTest entries for the new safety-net scripts and record any environment prerequisites or skipped scenarios. _(Registration verified with `ctest --test-dir build/linux-debug-clang -N -R 'Safety(UploadInvariants|ContentQuota|MoveCopyPath)Integration'`; execution skipped by user instruction.)_
- [x] 5.4 Run existing relevant integration tests (`UploadFlowIntegration`, `FileMutationOpsIntegration`, `CopyDeleteAtomicityIntegration`, `TrashLifecycleIntegration`, `FolderLifecycleIntegration`) to confirm no regression in existing coverage. _(Skipped by user instruction because Redis/backend runtime verification is unavailable in this environment.)_

## 6. Documentation and TODO Alignment

- [x] 6.1 Update `docs/TODO.md` Safety Net checkboxes that are fully covered by the new tests.
- [x] 6.2 Document any current product-rule observations captured by the tests, especially instant upload accounting and trash quota behavior.
- [x] 6.3 Leave any scenarios that could not be made deterministic as explicit pending TODO items with the blocker and recommended next step.
