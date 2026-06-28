## 1. Web Token Refresh Queue

- [x] 1.1 Update `clients/disk-web/src/api/client.ts` refresh subscriber state so queued requests have both resolve and reject paths.
- [x] 1.2 On refresh success, retry each queued Web owner request with the refreshed access token and settle the queued promise with the retry result.
- [x] 1.3 On refresh failure, reject every queued Web owner request with an authentication error, clear the queue, and clear invalid local tokens.
- [x] 1.4 Add or update Web API client tests that verify queued requests do not remain pending when refresh fails.

## 2. Web Authenticated Download Refresh

- [x] 2.1 Add shared Web fetch-download token handling that reads current owner tokens, refreshes once on 401/auth expiry, persists refreshed tokens, and retries the binary download request once.
- [x] 2.2 Update `clients/disk-web/src/composables/useDownload.ts` to use the refreshed-token fetch path for owner downloads while preserving progress and abort behavior.
- [x] 2.3 Keep share-token download flows isolated so visitor share downloads do not attach or refresh owner JWT credentials.
- [x] 2.4 Add Web download tests for expired-token download retry success and refresh failure propagation.

## 3. Web Real Pause/Resume Downloads

- [x] 3.1 Extend Web `DownloadTask` runtime state with received byte count, range support, total size, and partial chunk storage or equivalent continuation state.
- [x] 3.2 Update download streaming to append chunks, update received bytes, and compute progress from received bytes over total size.
- [x] 3.3 Change pause behavior to abort the active request while preserving partial bytes and received-byte state for range-capable downloads.
- [x] 3.4 Change resume behavior to request `Range: bytes=<received_bytes>-` for range-capable paused downloads and continue progress from the saved offset.
- [x] 3.5 Validate resumed responses with HTTP 206 and matching `Content-Range` before appending resumed bytes; fail or restart deterministically on mismatch.
- [x] 3.6 Ensure completion saves the assembled blob and cancellation/removal releases partial chunks and abort controllers.
- [x] 3.7 Add Web transfer-store/composable tests for pause preserving offset, resume sending Range, successful append, and range mismatch protection.

## 4. TUI Authenticated Download Refresh

- [x] 4.1 Add a raw/binary owner request helper in `clients/disk-tui/internal/client/client.go` that retries once after token refresh on HTTP 401/auth download failures.
- [x] 4.2 Update `DownloadFile` and `DownloadFileRange` in `clients/disk-tui/internal/client/file.go` to use the raw authenticated retry helper.
- [x] 4.3 Ensure TUI visitor share downloads continue to use share-token authentication without owner-token refresh.
- [x] 4.4 Add TUI client tests for owner download refresh/retry, refresh failure, and visitor share download isolation.

## 5. Verification

- [x] 5.1 Run the Web unit test suite covering API client, download composable, and transfer store changes.
- [x] 5.2 Run the TUI Go test suite covering client download/auth behavior.
- [x] 5.3 Run OpenSpec validation/status for `fix-token-refresh-download-resume` and confirm required artifacts are apply-ready.
