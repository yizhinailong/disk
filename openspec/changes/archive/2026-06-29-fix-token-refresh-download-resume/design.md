## Context

The Web client currently has two separate authenticated request paths. Axios-based API calls go through `clients/disk-web/src/api/client.ts`, which refreshes owner access tokens after a 401 and queues concurrent requests while refresh is in flight. Fetch-based download code in `clients/disk-web/src/composables/useDownload.ts` bypasses that interceptor by reading the current access token directly, so a stale token can be used for downloads. The TUI client has the same split: JSON envelope requests use `decodeEnvelope` and can refresh/retry, while raw binary downloads in `clients/disk-tui/internal/client/file.go` call `doRequest` directly and currently do not refresh/retry.

The Web download store in `clients/disk-web/src/stores/transfer.ts` exposes pause/resume state, but aborting a fetch only stops the in-flight request. Resume resets progress and starts a fresh download, so it is not a real continuation of already received bytes. The backend already supports download metadata and HTTP Range responses; this change uses those existing contracts instead of adding backend routes.

## Goals / Non-Goals

**Goals:**

- Make Web refresh queue failure deterministic: every request waiting on a failed refresh rejects with the same authentication error instead of hanging.
- Make protected owner downloads in Web and TUI use the same refresh-and-retry semantics as other owner API calls.
- Implement real Web download pause/resume using byte-range continuation from the received offset when the file supports ranges.
- Keep owner JWT and visitor share-token domains isolated.
- Add regression tests that reproduce the three reported failure modes.

**Non-Goals:**

- No backend endpoint changes; use existing `/auth/refresh`, `/file/download/{id}/info`, and `/file/download/{id}` Range support.
- No durable cross-browser/offline persistence for partial Web downloads beyond runtime task state.
- No change to visitor share-token refresh behavior; share tokens remain separate and are not refreshed through owner credentials.
- No full download accelerator or multi-range downloader.

## Decisions

1. **Replace callback-only Web refresh subscribers with promise settlement callbacks.**
   - Current callbacks only receive a successful token, so `onRefreshFailed()` can only clear the list and cannot reject waiting promises.
   - Store each queued request as `{ resolve, reject }` or equivalent. On refresh success, retry each request with the new token and resolve/reject with that retry result. On refresh failure, reject each queued promise with the auth error and clear the queue.
   - Alternative considered: dispatch a global logout event and let callers time out. Rejected because it still leaves per-request promises unresolved.

2. **Expose a single Web owner-download fetch path that can refresh before retrying once.**
   - Fetch downloads cannot rely on Axios response interceptors. Add shared helper logic around fetch downloads: build Authorization from current token, perform the request, detect 401/auth failure, refresh using the current refresh token if available, then retry once with the new access token.
   - `getDownloadInfo()` can continue using `apiClient`; the binary fetch must use the same token refresh source and token persistence semantics as `apiClient`.
   - Alternative considered: convert downloads to Axios blob requests. Rejected for resumable streaming because fetch `ReadableStream` gives direct progress and cancellation control.

3. **Teach TUI raw download requests to use the refresh retry path.**
   - Keep JSON envelope `decodeEnvelope` behavior, but add a raw/binary variant that retries once after refreshing when owner download returns HTTP 401/auth error.
   - `DownloadFile` and `DownloadFileRange` should use the raw authenticated retry helper. Share-token downloads must continue using share auth without owner refresh.
   - Alternative considered: force callers to call `Refresh()` manually before every download. Rejected because it duplicates policy at each call site and is easy to forget.

4. **Track Web partial download bytes and chunks in the download task runtime state.**
   - Extend the Web `DownloadTask` state with received byte count, total size, range support, and in-memory chunks or equivalent partial content state.
   - While streaming, append received chunks and update `received_bytes` and progress. Pausing aborts the fetch but preserves the collected chunks and received byte count. Resuming sends `Range: bytes=<received_bytes>-` when range is supported and appends the new response body to existing chunks. Completion saves the concatenated blob.
   - If range is unsupported, resuming must restart from byte 0 or present a clear non-resumable behavior; it must not claim continuation from the previous offset.
   - Alternative considered: persist partial bytes to IndexedDB. Deferred because the requested fix is for fake pause/continue in the current runtime flow, and durable persistence adds storage quota and cleanup complexity.

5. **Use explicit response validation for range continuation.**
   - When resuming from a non-zero offset, require HTTP 206 and a `Content-Range` whose start matches the requested offset. If the server returns 200, treat it as a restart only after discarding incompatible partial state and resetting progress; otherwise fail clearly.
   - Alternative considered: append whatever body arrives. Rejected because it can corrupt the saved file.

## Risks / Trade-offs

- **Queued request retry can fan out failures after one refresh failure** → Use one shared `ApiError` and reject all queued promises before redirecting/clearing tokens so callers can settle.
- **Fetch response formats differ between envelope errors and binary bodies** → Detect HTTP status first; only parse error bodies when present and avoid consuming successful binary streams before progress reading.
- **In-memory partial chunks can consume browser memory for large paused downloads** → Scope the implementation to runtime pause/resume and document that completed or cancelled tasks release chunks; consider IndexedDB/file-system persistence in a future change.
- **Range mismatch can produce corrupted downloads** → Validate 206/Content-Range on resumed requests and fail or restart deterministically rather than appending mismatched bytes.
- **TUI raw retry must not refresh visitor share downloads** → Keep request options explicit (`shareAuth`/`noAuth`) and only apply owner refresh to owner-authenticated download paths.

## Migration Plan

- Update Web token refresh queue implementation and tests first to prevent hanging callers.
- Update Web download helper/store/types to support owner-token refresh and runtime range continuation.
- Update TUI raw download helper paths to refresh/retry once for owner downloads.
- Add/adjust unit tests for Web API client, Web download composable/store, and TUI client downloads.
- Rollback is local to client code: revert to previous request helpers if regressions appear; no data migration or backend rollback is required.
