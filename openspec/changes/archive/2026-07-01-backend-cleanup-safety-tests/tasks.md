## 1. Deterministic Cleanup Seam

- [x] 1.1 Identify the existing scheduled cleanup entry point and confirm expired upload and expired trash cleanup both flow through `CleanupService` without duplicating logic
- [x] 1.2 Add or stabilize a deterministic test/manual cleanup trigger that invokes the same expired upload cleanup implementation used by scheduled cleanup
- [x] 1.3 Add or stabilize a deterministic test/manual cleanup trigger that invokes the same expired trash cleanup implementation used by scheduled cleanup
- [x] 1.4 Verify production scheduled cleanup cadence, batch limits, and behavior remain unchanged

## 2. Expired Upload Cleanup Safety Coverage

- [x] 2.1 Add integration-test fixtures/helpers to create an expired in-progress upload with reserved storage and temporary upload artifacts
- [x] 2.2 Add a safety test that runs deterministic expired upload cleanup and verifies the upload task becomes terminal or inactive
- [x] 2.3 Assert expired upload cleanup releases `users.storage_reserved`, creates no logical `files` row, and removes temporary upload artifacts

## 3. Expired Trash Cleanup Safety Coverage

- [x] 3.1 Add integration-test fixtures/helpers to create expired trash records with content references, used-storage accounting, and physical blob state
- [x] 3.2 Add a safety test that runs deterministic expired trash cleanup and verifies permanent-delete semantics for expired trash rows
- [x] 3.3 Assert expired trash cleanup updates content reference counts and releases used storage according to the current backend rule
- [x] 3.4 Assert physical blobs are deleted only after content has no remaining references, including a shared-content case that must preserve the blob

## 4. Upload Completion Dedup Race Coverage

- [x] 4.1 Add test setup for an upload initialized before matching `file_contents` exists
- [x] 4.2 Insert or create matching content before upload completion to simulate the dedup race
- [x] 4.3 Complete the upload and assert resulting file/content rows and reference counts are consistent with current backend behavior
- [x] 4.4 Ensure the test name or assertion messages document the observed current accounting rule

## 5. Test Integration and Verification

- [x] 5.1 Register any new safety scripts/tests with the existing CTest/uv integration-test contract and mark shared-state tests serial
- [x] 5.2 Skip targeted backend safety test execution per user instruction; CMake/Drogon runtime verification was unavailable in this worktree, while Python syntax and OpenSpec validation passed
- [x] 5.3 Run the relevant OpenSpec validation/status command for `backend-cleanup-safety-tests`
