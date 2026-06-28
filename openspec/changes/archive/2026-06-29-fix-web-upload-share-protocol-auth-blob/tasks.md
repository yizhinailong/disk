## 1. Contract Mapping

- [x] 1.1 Locate Web upload, share owner, visitor share download, and API interceptor modules in `clients/disk-web`.
- [x] 1.2 Compare Web upload initialization, chunk upload, and completion request shapes against backend file-transfer routes/controllers.
- [x] 1.3 Identify the existing authenticated owner API client and visitor share-token API client boundaries.

## 2. Web Upload Protocol Fix

- [x] 2.1 Update Web upload initialization handling to use backend returned upload task metadata and uploaded chunk state.
- [x] 2.2 Update chunk upload requests to send the backend upload task identifier, chunk index, binary chunk payload, and owner bearer authentication.
- [x] 2.3 Update upload completion/cancel paths to call the backend contract and preserve retry/cleanup behavior.

## 3. Share Authentication and Download Fix

- [x] 3.1 Route owner share management APIs through the authenticated owner client so `Authorization: Bearer <access_token>` is attached.
- [x] 3.2 Keep visitor share browse/download APIs on the share-token path using `X-Share-Token`.
- [x] 3.3 Configure shared-file downloads for Blob/binary response handling and bypass JSON envelope unwrapping for successful binary responses.
- [x] 3.4 Preserve JSON envelope error handling for failed downloads, including Blob-wrapped JSON error responses where applicable.

## 4. Regression Coverage

- [x] 4.1 Add or update Web tests/mocks that assert chunk upload request shape and owner bearer authentication.
- [x] 4.2 Add or update Web tests/mocks that assert owner share APIs include the bearer JWT.
- [x] 4.3 Add or update Web tests/mocks that assert visitor share downloads send `X-Share-Token`, request Blob content, and return Blob responses without envelope parsing.
- [x] 4.4 Run the relevant Web typecheck/test command and record the result.
