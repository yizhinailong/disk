## Context

The change spans the Web owner explorer and Desktop transfer manager. In the Web client, `FolderTree.vue` currently owns its own `rootChildren` and lazy-loads `/folder/tree` directly, while `useDriveStore` also has a `folderTree` state/action that is not used by the component. Navigation changes update the file list and breadcrumb, but folder-tree data is not refreshed when folder-affecting operations occur, so the tree can become stale after create/rename/move/delete or after navigating through another surface.

In the Desktop client, `StartShareDownload` creates a visitor download task from share-browse/caller-provided metadata and calls `PreparePartialFileForRange` before marking the visitor task as range-capable. Because a newly created visitor task defaults `supports_range` to false, partial visitor files fall back to full download and may be removed even though the backend share download response path accepts `Range`. `StartDownloadTransfer` already supports owner/visitor auth-domain header selection and adds `Range` when the task enters range mode. `HandleDownloadFinished` currently marks the task completed after a successful network reply by setting `received_bytes = file_size`, without checking the actual file length or expected hash.

## Goals / Non-Goals

**Goals:**

- Make the Web folder tree a store-backed, refreshable view of the current folder hierarchy.
- Invalidate or refresh folder-tree data after folder-affecting mutations and after navigation paths that reveal tree drift.
- Let Desktop visitor downloads resume from existing partial files when share-browse/caller-provided metadata supplies the expected size and the share download response path supports ranges.
- Validate Desktop completed downloads against actual byte size and expected hash when metadata provides a hash.
- Preserve owner/visitor auth separation during metadata, full download, and range download requests.

**Non-Goals:**

- Add new backend routes or change backend storage semantics.
- Implement cross-session download task persistence beyond the existing local partial-file resume behavior.
- Add new hash algorithms unless the existing metadata exposes them; the implementation should use the existing file hash field first.
- Redesign the Web file explorer or Desktop transfer UI beyond the states needed to surface refresh, resume, and validation failures.

## Decisions

1. **Use the drive store as the Web folder-tree source of truth.**
   `FolderTree.vue` should render from `driveStore.folderTree` and call `driveStore.fetchFolderTree()`/refresh actions instead of maintaining an independent `rootChildren` copy. This keeps breadcrumb/list/tree consumers aligned and makes tests target store behavior. Alternative considered: keep component-local tree state and emit refresh events from every mutation site. That spreads invalidation logic across the UI and leaves the store's existing `folderTree` unused.

2. **Add explicit folder-tree invalidation after folder-affecting operations.**
   After create folder, rename folder, move folder, delete/trash folder, restore folder, or any operation that can change parent/child hierarchy or folder names, the Web client should refresh the current file list/breadcrumb and folder tree together. Alternative considered: refresh the tree only on page mount. That reduces API calls but leaves visible stale nodes for the active session.

3. **Prepare partial Desktop downloads only after expected size and range support are known.**
   Owner downloads should continue to fetch metadata before deciding whether an existing local file can be resumed. Visitor downloads should use share-browse/caller-provided file name and size, mark the share download response path as range-capable, then call `PreparePartialFileForRange` before starting transfer. Alternative considered: fetch visitor metadata from the share download URL. The backend share download URL streams file content, so treating it as JSON metadata would corrupt the flow.

4. **Treat completion validation as part of transfer finalization.**
   `HandleDownloadFinished` should close the file, inspect the actual target path length, and compare it with the expected size before setting `Completed`. If `file_hash` is present, compute the local hash and compare it before completion. Size/hash mismatches should fail the task with a non-retryable integrity error and avoid setting progress to 100%. Alternative considered: rely on HTTP success and progress counters. That accepts truncated or corrupted files when the connection or server misreports completion.

5. **Use existing auth-domain header helpers for visitor range requests.**
   Visitor resume must continue through `PrepareVisitorHeaders()` so `X-Share-Token` is present alongside `Range`. No dual-auth or owner JWT fallback should be introduced for visitor downloads. Alternative considered: special-case share download requests in the transfer code. That would duplicate auth behavior already centralized in `RequestFactory`.

## Risks / Trade-offs

- **Extra Web folder-tree API calls** → Refresh only after hierarchy-affecting operations and navigation recovery, not after every file-only operation.
- **Lazy tree expansion state may reset after refresh** → Preserve the active folder highlight and expand the ancestor path when possible after replacing tree data.
- **Partial file larger than expected file size** → Delete or ignore the partial file and restart full download, because no valid range can resume beyond the expected size.
- **Hash field algorithm ambiguity** → Reuse the existing `ComputeFileMd5` path only when the metadata hash format matches the current expected file hash contract; otherwise add a small adapter or fail with a clear unsupported-validation error rather than silently skipping required validation.
- **Validation cost for large files** → Perform hash validation after network completion and report a validating/finalizing state if needed; size validation remains cheap and mandatory.
