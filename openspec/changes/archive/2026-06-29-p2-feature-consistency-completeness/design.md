## Context

This change closes P2 feature consistency and completeness gaps across Web, Desktop, shared client behavior, and backend transfer/admin contracts. Existing specs already cover administrator authorization/user management, folder navigation metadata, ranged file download, cross-client REST behavior, Desktop shell separation, and validation discipline. The missing parts are client-facing completeness: Web administrators need storage/quota editing, Web folder tree state must be centralized instead of drifting from list/breadcrumb state, large Web downloads must avoid buffering full payloads, and Desktop visitor downloads must resume and validate real file content using backend metadata and Range support.

The implementation should prefer existing REST APIs where they already satisfy requirements. Backend contract changes are limited to exposing explicit admin storage/quota mutation and download metadata needed for integrity verification when those are not already available.

## Goals / Non-Goals

**Goals:**

- Provide Web admin UI and store/API integration for modifying a user's storage quota or storage-related administrative value safely.
- Make the Web folder tree consume and update centralized store state so folder tree, list, breadcrumb, current folder, selection, and refresh behavior remain consistent.
- Implement Web large-file download behavior that streams or otherwise avoids retaining the full response in application memory.
- Implement Desktop visitor download resume using share-token authenticated byte-range requests and persisted partial download state.
- Verify completed downloads against expected byte size and available hash/checksum metadata, and handle validation failures explicitly.
- Add validation coverage for the above behavior in unit, integration, system, and performance/pressure checks where appropriate.

**Non-Goals:**

- Redesigning the complete Web or Desktop information architecture.
- Introducing a new transfer protocol beyond the existing REST and HTTP Range model.
- Changing upload lifecycle behavior except where shared transfer validation references download integrity.
- Replacing backend authorization, quota accounting, or storage engines beyond the minimal APIs required for this change.
- Adding unsupported platform-specific Desktop behavior outside the existing Linux-first, Windows-ready Qt/QML constraints.

## Decisions

1. **Use backend-owned rules for admin storage changes.**
   Web will present and submit storage/quota edits, but backend admin APIs will validate administrator role, target user existence, allowed bounds, quota-used consistency, self/last-admin safety where relevant, and audit/result recording. This avoids duplicating quota business rules in the Web client. Alternative considered: client-side-only validation; rejected because it cannot protect direct API use and would drift across clients.

2. **Centralize Web folder tree state in the existing client store layer.**
   Folder tree expansion, selected/current folder, refresh triggers, and navigation side effects should flow through the store that also drives file list and breadcrumb context. Components should render state and dispatch actions rather than maintain isolated authoritative state. Alternative considered: synchronizing local component refs manually; rejected because it preserves the current drift risk.

3. **Use HTTP Range as the single resume mechanism.**
   Desktop visitor resume and any cross-client resume behavior will append to an existing partial file only after confirming local size and remote metadata, then request `Range: bytes=<local-size>-` with the visitor share token. Alternative considered: custom checkpoint endpoints; rejected because the backend already specifies ranged downloads and HTTP Range works for both owner and visitor domains.

4. **Make metadata-driven integrity verification mandatory when metadata is available.**
   Clients must always validate expected byte count, and must validate hash/checksum when the download metadata includes one. If no hash is exposed for a given visitor flow, size validation still applies and the missing hash is a backend contract gap to address separately. Alternative considered: relying on HTTP success alone; rejected because interrupted or corrupted partial files can still produce misleading UI success.

5. **Avoid full in-memory Web download buffering.**
   Web download code should use browser-supported streaming or object-URL/file-save behavior that does not accumulate large files in reactive state or duplicate buffers unnecessarily. Progress, cancellation, and errors should be surfaced without storing the payload in the app store. Alternative considered: `arrayBuffer()`/full `Blob` assembly for all downloads; rejected because it creates memory pressure for large files.

6. **Treat validation as release evidence, not only implementation tests.**
   The validation capability will include scenario-level evidence requirements for admin storage edits, folder tree state consistency, resumed visitor downloads, integrity failures, and Web large-file memory pressure. Alternative considered: relying only on manual exploratory checks; rejected because P2 parity gaps need reproducible regression coverage.

## Risks / Trade-offs

- **Backend metadata may not currently expose hashes for every download domain** → Add or reuse metadata fields where available; require size verification regardless and document missing hash coverage as a contract issue until resolved.
- **Browser streaming support differs by environment** → Provide a fallback path that still avoids app-store/reactive buffering where possible, and validate behavior in supported browsers.
- **Partial-file resume can corrupt output if remote content changed** → Resume only when file identity metadata matches; otherwise discard or restart the partial file with clear user feedback.
- **Admin quota edits can create unsafe account states** → Enforce lower bounds relative to used storage and perform authorization/audit on the backend, not only in Web.
- **Centralizing folder tree state may reveal existing component assumptions** → Migrate navigation entry points incrementally behind store actions and add tests for tree/list/breadcrumb consistency.

## Migration Plan

1. Confirm existing backend endpoints and metadata fields for admin user detail/update, file download info, visitor share browse/download, and Range support.
2. Add minimal backend deltas only where required: admin storage/quota update and/or download metadata integrity fields.
3. Update Web admin store/API wrappers and admin user UI to edit storage quota with validation and refresh behavior.
4. Refactor Web folder tree navigation to read/write the centralized store and remove conflicting local authoritative state.
5. Update Web download service to avoid full application-memory buffering and preserve progress/error feedback.
6. Update Desktop visitor download manager to persist partial state, send Range requests, append safely, and validate completion.
7. Add automated and manual validation evidence for success, failure, resume, integrity mismatch, and pressure scenarios.
8. Roll back by disabling new UI actions and falling back to full-download behavior only if integrity/resume validation fails before release; backend changes should remain backward compatible.

## Open Questions

- Which exact backend field should be treated as the canonical downloadable content hash for visitor downloads if multiple hash/checksum fields exist?
- Should Web admin storage editing allow only quota changes, or also administrative used-space correction? The default implementation should expose quota changes only unless an existing backend contract already supports safe used-space correction.
- Which browser versions are the minimum supported targets for streaming download validation?
