## Why

Web folder-tree state can drift from the active directory after namespace changes, making navigation stale or misleading. Desktop visitor downloads also need the same resilient resume behavior and completion integrity checks expected from owner downloads so interrupted transfers do not restart unnecessarily and completed files are not accepted when size or hash validation fails.

## What Changes

- Keep the Web folder tree synchronized with directory navigation and namespace mutations that affect folder hierarchy.
- Extend Desktop visitor downloads to resume from partial local files when the shared download endpoint supports byte ranges.
- Validate Desktop download completion against expected size and, when available, expected content hash before marking a task completed.
- Surface validation or resume failures as failed transfer states with actionable errors instead of silently accepting incomplete or corrupted files.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `file-namespace`: Folder tree navigation metadata must remain refreshable and consistent after folder-affecting operations.
- `file-transfer`: Download workflows must support resumable client behavior and completion integrity validation.
- `client-integration`: Visitor share download clients must use share-token authentication consistently when resuming downloads.
- `desktop-client-experience`: Desktop visitor transfer UX must expose resumable download behavior and validation failures consistently with owner transfers.

## Impact

- Web file explorer folder-tree state management and refresh/invalidation triggers.
- Desktop transfer manager, download task model/state, and visitor share download request construction.
- Shared API consumption for owner and visitor ranged downloads, including `Range` and `X-Share-Token` headers.
- Tests or verification covering folder-tree sync, visitor resume, and completed-download size/hash validation.
