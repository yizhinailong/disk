## Why

Web token refresh currently leaves queued requests unresolved when refresh fails, which can hang callers instead of surfacing authentication failure. Download flows also rely on stale authorization state: Web/TUI download requests do not refresh tokens before downloading, and Web pause/continue behaves like a UI-only restart rather than a real resumable transfer.

## What Changes

- Ensure Web access-token refresh fan-out settles every queued request: successful refresh retries queued requests, failed refresh rejects them and clears authentication state consistently.
- Require Web and TUI authenticated download paths to refresh owner access tokens before protected download requests when refresh is possible, then retry once using the refreshed token.
- Replace Web fake pause/continue download behavior with real resumable semantics based on byte ranges, local partial progress, cancellation, and continuation from the received offset.
- Preserve visitor share-token isolation: share download flows continue to use share tokens and do not reuse owner refresh state.
- Add regression coverage for refresh failure queue rejection, token-refresh-before-download behavior, and resumable pause/continue download behavior.

## Capabilities

### New Capabilities

- `web-resumable-downloads`: Web client download task behavior for real pause, continuation, cancellation, progress accounting, and byte-range resumption.

### Modified Capabilities

- `client-integration`: Strengthens cross-client token refresh expectations for queued Web requests and authenticated Web/TUI download paths.
- `file-transfer`: Clarifies that clients using ranged downloads can resume from persisted/known offsets and must request byte ranges that match already received content.

## Impact

- Affected code: Web API client token refresh interceptor, Web file/share download helpers, Web download manager/store/UI state, TUI download command/client code, and related tests.
- Affected APIs: no backend route changes are expected; existing refresh-token and HTTP Range download contracts are used.
- Affected behavior: callers no longer hang on refresh failure, protected downloads avoid stale access tokens when refresh is possible, and Web pause/continue resumes transferred content instead of restarting or only changing UI state.
