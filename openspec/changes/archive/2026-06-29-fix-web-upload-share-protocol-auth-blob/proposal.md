## Why

The Web client currently diverges from the backend API contract in three user-visible flows: chunked uploads use an incompatible protocol, share owner APIs may be called without the owner JWT, and shared file downloads can be corrupted when binary Blob responses are treated as JSON envelopes. Fixing these contract mismatches restores reliable Web upload, owner share management, and share download behavior.

## What Changes

- Align the Web chunk upload workflow with the backend upload lifecycle and chunk request contract.
- Ensure Web share owner management calls send the authenticated user's bearer JWT, not visitor share-token-only headers or unauthenticated requests.
- Make Web shared-file downloads request and handle binary Blob responses outside the JSON envelope interceptor path while preserving JSON error handling.
- Add regression coverage for Web upload, owner share API authentication headers, and share download Blob handling.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `file-transfer`: Clarify that Web chunk upload requests must follow the backend resumable upload and completion contract.
- `sharing`: Clarify the distinction between authenticated owner share management and visitor share download behavior.
- `api-contract`: Clarify that successful binary downloads are exempt from JSON envelope processing in clients/interceptors.
- `client-integration`: Require Web client implementations to send the correct token type per API domain and preserve binary download responses.

## Impact

- Web upload client code and request payload/header construction.
- Web share owner API service calls and authentication interceptor behavior.
- Web share download request configuration and response interceptors.
- Tests or mocks covering upload chunks, owner share management authorization, and Blob download responses.
