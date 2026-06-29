## Why

P2 feature parity gaps leave Web and Desktop behavior inconsistent with the shared product contract: administrators cannot adjust user storage from Web, Web navigation/download flows are not robust enough for large trees/files, and Desktop visitor downloads do not yet provide true resume or completion integrity guarantees. Addressing these together keeps client behavior complete, consistent, and safe before broader validation or release work.

## What Changes

- Add administrator-facing user storage/quota modification capability to the Web client, backed by explicit admin-operation requirements and safety constraints.
- Connect the Web folder tree to the centralized store so folder context, selection, refresh, breadcrumb, and list state stay consistent across navigation entry points.
- Make Desktop visitor downloads use backend byte-range support for real breakpoint resume instead of only restarting or simulating progress.
- Add client-side download completion verification for expected size and available hash/checksum metadata, with clear failure handling.
- Change Web large-file downloads to avoid loading full file payloads into memory, using streaming/blob-safe download behavior and visible error handling.
- No breaking changes are intended; existing APIs should be reused where they already provide required metadata/range behavior, with backend deltas only where explicit storage-admin or integrity metadata is missing.

## Capabilities

### New Capabilities
- `web-client-experience`: Defines Web client UX/state requirements for admin storage editing, folder tree store integration, and memory-safe large-file downloads.

### Modified Capabilities
- `admin-operations`: Add administrator user storage/quota management behavior and safety constraints.
- `client-integration`: Add cross-client download resume, completion verification, and memory-safe transfer expectations.
- `desktop-client-experience`: Add Desktop visitor download resume and integrity-verification behavior.
- `file-transfer`: Clarify download metadata/range/integrity requirements needed by clients for resumable and verified downloads.
- `validation-and-performance`: Add validation coverage for Web large-file memory pressure, folder tree state consistency, admin storage editing, and Desktop visitor resumed/verified downloads.

## Impact

- Web client admin user-management UI, folder tree/store modules, download service/composables, and error/progress presentation.
- Desktop visitor-shell download manager, share-token download flow, local partial-file handling, resume state, and post-download validation.
- Backend admin APIs if user storage/quota mutation is not already exposed; file/download metadata if expected size/hash is not already returned to clients.
- Shared REST client behavior around Range requests, streaming downloads, retry/resume, and integrity failure handling.
- Test and validation plans for client consistency, large-file pressure, resumed downloads, and storage administration safety.
