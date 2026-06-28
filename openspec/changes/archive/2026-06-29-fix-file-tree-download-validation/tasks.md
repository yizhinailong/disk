## 1. Web Folder Tree Synchronization

- [x] 1.1 Refactor `clients/disk-web/src/stores/drive.ts` so `folderTree` is the authoritative tree state and add a refresh/invalidate action that fetches `/folder/tree` and preserves current folder context.
- [x] 1.2 Refactor `clients/disk-web/src/components/drive/FolderTree.vue` to render from the drive store instead of maintaining independent `rootChildren` state, while preserving current-node highlight and useful expanded ancestors.
- [x] 1.3 Update Web folder-affecting operation flows to refresh both current view data and folder-tree metadata after create, rename, move, trash/delete, and restore operations.
- [x] 1.4 Add or update Web tests for store-backed folder tree refresh, navigation highlight synchronization, and mutation-triggered tree refresh.

## 2. Desktop Visitor Download Resume

- [x] 2.1 Update `TransferManager::StartShareDownload` so visitor download tasks use share-browse/caller-provided metadata and know range support before deciding whether a partial target file can be resumed.
- [x] 2.2 Keep partial-file range preparation on the post-metadata path for owner downloads and the post-initialization path for visitor downloads, including handling partial files larger than the expected size by restarting full download.
- [x] 2.3 Ensure visitor range requests use `PrepareVisitorHeaders()` with both `X-Share-Token` and `Range` headers and never require owner bearer authentication.
- [x] 2.4 Add or update Desktop transfer tests proving visitor downloads resume with range when supported and restart full transfer when resume is unavailable or rejected.

## 3. Desktop Download Completion Validation

- [x] 3.1 Update `TransferManager::HandleDownloadFinished` to validate the actual local file size before marking owner or visitor downloads completed.
- [x] 3.2 Add hash validation using existing download metadata and hash utilities when `file_hash` is present, failing the task on mismatch before completion progress is emitted.
- [x] 3.3 Surface size/hash validation failures as non-completed failed download tasks with actionable integrity error messages.
- [x] 3.4 Add or update Desktop transfer tests for successful validation, size mismatch failure, hash mismatch failure, and no-hash size-only completion.

## 4. Verification

- [x] 4.1 Run the Web unit tests covering drive store and folder tree behavior.
- [x] 4.2 Run the Desktop transfer/unit tests covering visitor resume and completion validation.
- [ ] 4.3 Manually verify the Web folder tree stays synchronized after folder creation/rename/move/delete and navigation.
- [ ] 4.4 Manually verify Desktop visitor download resume and completed-download size/hash validation behavior against a shared file.
