## 1. Backend Contract Verification and Gaps

- [x] 1.1 Inventory existing admin user detail/update, storage quota fields, file download metadata, owner download, and visitor share download APIs.
- [x] 1.2 Add or adjust administrator storage quota update API if no existing endpoint satisfies the admin storage management spec.
- [x] 1.3 Ensure admin storage updates enforce administrator authorization, target-user validation, quota-used consistency, and administrative operation result recording.
- [x] 1.4 Ensure owner and visitor download metadata expose expected byte size and available hash/checksum fields permitted by the API contract.
- [x] 1.5 Ensure visitor share downloads accept valid HTTP Range requests with share-token authentication and reject invalid ranges explicitly.

## 2. Web Admin Storage Editing

- [x] 2.1 Add Web API client/store actions for administrator storage quota updates and affected user refresh.
- [x] 2.2 Add storage quota edit entry points to the Web admin user list or user detail UI.
- [x] 2.3 Implement quota edit form validation, current used/quota display, submit loading state, success feedback, and backend error feedback.
- [x] 2.4 Refresh user list/detail state after successful quota update and avoid showing unsaved edited values after failures.

## 3. Web Folder Tree Store Integration

- [x] 3.1 Identify existing folder tree, file list, breadcrumb, current folder, and selection state ownership in the Web client.
- [x] 3.2 Move folder tree data, selected/current folder, expansion/refresh triggers, and navigation side effects into the centralized store.
- [x] 3.3 Update folder tree, breadcrumb, file-list drill-down, up navigation, and folder mutation flows to dispatch store actions instead of maintaining conflicting local authoritative state.
- [x] 3.4 Add recoverable folder tree refresh error handling that preserves the last safe current folder/list state.

## 4. Web Memory-Safe Downloads

- [x] 4.1 Audit current Web download code for full-response buffering, reactive payload storage, and large-file memory pressure points.
- [x] 4.2 Implement a streaming, browser download primitive, or safest-supported fallback path that keeps file payload bytes out of centralized UI state.
- [x] 4.3 Preserve progress, cancellation/interruption, authorization failure, range/download failure, and completion feedback in the updated download flow.
- [x] 4.4 Add Web download tests or manual validation hooks for large-file behavior and failure feedback.

## 5. Desktop Visitor Resume and Verification

- [x] 5.1 Inventory Desktop visitor share download flow, download manager state, local file write path, and share-token request handling.
- [x] 5.2 Persist partial download state with remote identity, expected total size, local partial size, and available integrity metadata.
- [x] 5.3 Implement safe resume using `Range: bytes=<local-size>-` with `X-Share-Token`, appending only when metadata confirms the partial file is trusted.
- [x] 5.4 Restart or fail explicitly when partial state is unsafe, range is rejected, or remote metadata no longer matches.
- [x] 5.5 Verify completed Desktop visitor downloads against expected byte size and available hash/checksum metadata before reporting success.
- [x] 5.6 Surface clear Desktop visitor feedback for resumed, restarted, completed, verification-failed, and retryable download states.

## 6. Validation and Regression Coverage

- [x] 6.1 Add backend tests for admin storage quota update authorization, valid update, invalid quota rejection, and operation-result recording.
- [x] 6.2 Add backend/API tests for visitor ranged download success, invalid range rejection, and download metadata integrity fields.
- [x] 6.3 Add Web tests for admin quota editing success/failure and user state refresh.
- [x] 6.4 Add Web tests for folder tree/list/breadcrumb state consistency after tree selection, breadcrumb navigation, drill-down, and folder mutation.
- [x] 6.5 Add Web large-file validation evidence showing payload bytes are not stored in centralized UI state and terminal feedback is correct.
- [x] 6.6 Add Desktop tests or reproducible manual checks for visitor interrupted download resume, unsafe partial restart/failure, size verification, hash/checksum verification, and mismatch handling.
- [x] 6.7 Run focused backend, Web, and Desktop validation commands and record evidence or known blockers.
