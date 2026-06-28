## Context

The existing backend contract already separates three domains that the Web client must respect: authenticated owner APIs use `Authorization: Bearer <access_token>`, visitor share APIs use `X-Share-Token`, and successful binary downloads are returned outside the JSON envelope. The current Web behavior violates that contract in targeted places: chunked upload requests do not match the backend resumable upload protocol, share owner management calls can lose the owner JWT, and share download Blob responses can flow through envelope parsing intended only for JSON.

## Goals / Non-Goals

**Goals:**

- Make Web chunk upload initialize, upload chunks, and complete using the backend upload identifiers, chunk indexes, payload shape, and authentication headers expected by the file-transfer API.
- Make owner share management Web API calls consistently use the owner bearer access token.
- Make visitor share download requests keep using the share token while treating successful responses as binary Blob data rather than JSON envelopes.
- Preserve standard JSON envelope handling for non-download JSON APIs and for download failures that return JSON errors.
- Add focused regression coverage around request headers, upload request shape, and Blob response handling.

**Non-Goals:**

- Redesigning the backend upload, share, or API envelope contracts.
- Changing share-token issuance, expiry, permissions, or password verification rules.
- Introducing a new download transport, streaming mechanism, or resumable download feature.
- Reworking unrelated desktop/TUI clients unless shared types or contracts require updates.

## Decisions

1. Keep backend contracts authoritative and adapt Web request construction to them.
   - The upload fix should update the Web upload service to consume backend initialization results, upload only required chunks, identify each chunk with the backend upload task and chunk index, and call completion using the backend contract.
   - Alternative considered: add backend compatibility aliases for the Web protocol. This was rejected because it would expand the API surface and hide client drift.

2. Preserve separate owner and visitor authentication paths.
   - Owner share APIs must go through the normal authenticated API client/interceptor so `Authorization: Bearer <access_token>` is attached and refresh/retry behavior remains available.
   - Visitor share browse/download APIs must continue to send `X-Share-Token` and must not substitute the owner token.
   - Alternative considered: use one share API client for all share calls. This was rejected because owner and visitor calls have different trust domains and token headers.

3. Bypass JSON envelope unwrapping only for successful binary download responses.
   - Share downloads should request `responseType: 'blob'` (or the equivalent local client setting) and the response interceptor should return the Blob for successful binary responses instead of trying to read `code/message/data`.
   - JSON error responses from failed downloads should still be normalized into the existing API error path where possible.
   - Alternative considered: disable interceptors for all share endpoints. This was rejected because browse/access JSON APIs still rely on normal envelope handling.

4. Use focused client-level tests instead of broad end-to-end tests for the first regression layer.
   - Mocking the HTTP client/interceptor boundary can verify exact headers, request payloads, response type, and Blob pass-through deterministically.
   - End-to-end/manual verification can still be run during implementation, but the planned automated coverage should target the contract mismatches directly.

## Risks / Trade-offs

- Web upload request shape still differs from backend due to an overlooked field name or multipart/form-data detail → Mitigate by comparing implementation against backend route/controller contract and adding a mocked request assertion for chunk upload.
- Binary download errors may arrive as Blob-wrapped JSON, depending on browser/client behavior → Mitigate by detecting JSON content types or parsing error Blobs before surfacing failures.
- Share owner calls may accidentally use the visitor share client because function names overlap → Mitigate by separating owner share API functions from visitor share API functions and covering both header types in tests.
- Interceptor changes could affect non-share file downloads → Mitigate by applying binary pass-through based on response type/content type and adding at least one normal JSON envelope regression test.
