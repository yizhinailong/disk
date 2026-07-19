# Backend Discovery Notes

This note captures the backend behavior behind the `backend-discovery` OpenSpec change and the decisions subsequently applied to it. Historical risks are retained where useful, while the configuration descriptions below reflect the current target behavior.

## Scope and Evidence

Primary sources inspected:

- Global filter configuration: `config.json:49`
- File routes: `src/controllers/FileController.hpp:27`
- Folder routes: `src/controllers/FolderController.hpp:23`
- Auth routes: `src/controllers/AuthController.hpp:23`
- Share routes: `src/controllers/ShareController.hpp:43`
- Admin routes: `src/controllers/AdminController.hpp:40`
- JWT filter: `src/filters/JwtAuthFilter.cpp:24`
- Rate-limit filters: `src/filters/UploadRateLimitFilter.cpp`, `src/filters/DownloadRateLimitFilter.cpp`, `src/filters/RegisterRateLimitFilter.cpp`, `src/filters/ShareRateLimitFilter.cpp`, `src/filters/AdminRateLimitFilter.cpp`, `src/filters/FolderRateLimitFilter.cpp`
- Share-token authentication: `src/filters/ShareAuthFilter.cpp`
- Upload lifecycle: `src/services/UploadService.cpp:61`, `src/services/UploadService.cpp:368`, `src/services/UploadService.cpp:550`, `src/services/UploadService.cpp:983`
- Cleanup lifecycle: `src/services/CleanupService.cpp:48`, `src/services/CleanupService.cpp:315`
- File mutation lifecycle: `src/services/FileMutationService.cpp:413`, `src/services/FileMutationService.cpp:904`
- Download behavior: `src/controllers/FileController.cpp:313`, `src/controllers/ShareController.cpp:339`, `src/services/FileQueryService.cpp:355`, `src/services/ShareService.cpp:864`

## Filter and Rate-limit Behavior

### Global filters

`config.json` registers exactly one `drogon::plugin::GlobalFilters` entry:

```text
GlobalFilters
  filters:
    - disk::filters::RequestTraceFilter
    - disk::filters::JwtAuthFilter
    - disk::filters::RegisterRateLimitFilter
  exempt: []
```

Source: `config.json:49`.

### Route-level filters

Representative route-level filters:

| Route family | Examples | Route-level filters |
| --- | --- | --- |
| Upload | `/api/file/upload/init`, `/api/file/upload/chunk`, `/api/file/upload/complete`, `/api/file/upload/{upload_id}` | `UploadRateLimitFilter` |
| File query/mutation | `/api/file/list`, `/api/file/{file_id}`, `/api/file/move`, `/api/file/copy`, `/api/file` | none beyond global JWT |
| Private download | `/api/file/download/{file_id}/info`, `/api/file/download/{file_id}` | `DownloadRateLimitFilter` |
| Folder | `/api/folder/create`, `/api/folder/tree`, `/api/folder/{folder_id}/breadcrumb`, `/api/folder/{folder_id}/rename` | `FolderRateLimitFilter` |
| Auth public | `/api/auth/register`, `/api/auth/login`, `/api/auth/refresh` | none |
| Auth logout | `/api/auth/logout` | none beyond global JWT |
| Share owner | `/api/share`, `/api/share/{share_id}`, `/api/share/cancel` | none beyond global JWT |
| Share public access | `/api/share/access/{share_id}` | `ShareAccessRateLimitFilter` |
| Share browse | `/api/share/browse/{share_id}` | `ShareAuthFilter`, then `ShareOperationRateLimitFilter` |
| Share download | `/api/share/download/{share_id}/{file_id}/info`, `/api/share/download/{share_id}/{file_id}` | `ShareAuthFilter`, then `ShareOperationRateLimitFilter` |
| Share save | `/api/share/save/{share_id}` | `ShareAuthFilter`, then `ShareOperationRateLimitFilter`, in addition to global JWT |
| Admin | `/api/admin/...` | `AdminAuthFilter`, `AdminRateLimitFilter` |

Sources: `src/controllers/FileController.hpp:27`, `src/controllers/FolderController.hpp:23`, `src/controllers/AuthController.hpp:23`, `src/controllers/ShareController.hpp:43`, `src/controllers/AdminController.hpp:40`.

### JWT execution risk

The JWT filter validates the bearer token and inserts `user_id`, `username`, `role`, and `status` into request attributes before route-level filters and controllers execute. Public-path exemptions are centralized in `JwtAuthFilter::IsPublicPath`.

Confirmed framework behavior: Drogon `GlobalFilters` registers its configured plugin instance as pre-routing advice and runs its filter list before routing. Drogon then routes the request and runs route-level middlewares/filters before handling. Because plugin instances are addressed by plugin name, duplicate `drogon::plugin::GlobalFilters` entries are unsafe: a later entry can replace the earlier configuration. The configuration therefore uses one entry containing all global filters, and the ownership test asserts that uniqueness.

Conclusion: protected routes receive JWT exactly once before their route-owned authorization or rate-limit filters. Public routes are skipped by JWT's explicit path predicate and may still be handled by self-scoped public rate-limit or share-token filters.

### Rate-limit predicates and failure policy

Rate-limit filters are path scoped internally:

| Filter | Keying | Path predicate | Redis failure behavior |
| --- | --- | --- | --- |
| `UploadRateLimitFilter` | user id + fixed window | route-level upload routes | logs error and returns `nullptr` (fail-open) |
| `DownloadRateLimitFilter` | user id + fixed window | `path.starts_with("/api/file/download/")` | logs error and returns `nullptr` (fail-open) |
| `RegisterRateLimitFilter` | client IP + fixed window | `path == "/api/auth/register"` | logs error and returns `nullptr` (fail-open) |
| `ShareAccessRateLimitFilter` | normalized client IP + fixed window | route-owned `/api/share/access/{share_id}` | logs operation and normalized IP, then returns `nullptr` (fail-open) |
| `ShareOperationRateLimitFilter` | verified Share Token JTI + fixed window | route-owned browse, download metadata/content, and save routes; browse and download use separate buckets | logs operation without key/token material, then returns `nullptr` (fail-open) |
| `AdminRateLimitFilter` | user id + fixed window | `path.starts_with("/api/admin/")` | logs error and returns `nullptr` (fail-open) |
| `FolderRateLimitFilter` | user id + fixed window | `path.starts_with("/api/folder/")` | logs error and returns `nullptr` (fail-open) |

Sources: `src/filters/UploadRateLimitFilter.cpp`, `src/filters/DownloadRateLimitFilter.cpp`, `src/filters/RegisterRateLimitFilter.cpp`, `src/filters/ShareRateLimitFilter.cpp`, `src/filters/ShareAuthFilter.cpp`, `src/filters/AdminRateLimitFilter.cpp`, `src/filters/FolderRateLimitFilter.cpp`.

Tests cover individual path predicates and constants for several filters, including download and admin filters. `FilterOwnership_test.cpp` additionally validates that JWT appears once, route declarations do not duplicate it, and `GlobalFilters` itself is configured exactly once.

## Upload Lifecycle

### State model

```text
none
  │ InitUpload normal
  ▼
status=0 uploading
  ├─ CompleteUpload success ──▶ status=1 completed
  ├─ CancelUpload ───────────▶ status=2 cancelled
  └─ CleanupExpiredUploadTasks ─▶ status=3 expired
```

### Init upload

`UploadService::InitUpload` starts at `src/services/UploadService.cpp:61`.

Current behavior:

1. Validate configured max file size.
2. Run a compound query for folder location, filename collision, existing content by MD5 hash, and existing in-progress upload task.
3. If existing content is found, use instant upload:
   - Start transaction.
   - Re-check filename.
   - Increment `file_contents.ref_count`.
   - Insert a `files` row.
   - Do not increase `storage_used` for instant upload.
4. If an existing task is found:
   - If expired, delete the task and clean temp files.
   - Otherwise, return resume information with uploaded chunks.
5. For normal upload:
   - Reserve quota through `ReserveStorageQuota`.
   - Insert `upload_tasks` with `status = 0` and `reserved_bytes = file_size`.
   - Ensure temp upload directory.

Important current behavior: the inline expired-task branch in `InitUpload` deletes an expired task and cleans temp storage, but static inspection did not show a matching release of `users.storage_reserved` in that path.

Sources: `src/services/UploadService.cpp:61`, `src/services/UploadService.cpp:128`, `src/services/UploadService.cpp:226`, `src/services/UploadService.cpp:276`, `src/services/UploadService.cpp:1048`.

### Upload chunk

`UploadService::UploadChunk` starts at `src/services/UploadService.cpp:368`.

Current behavior:

1. Read short-TTL cached task or fetch task from DB.
2. Reject expired tasks.
3. Validate chunk index and expected chunk size.
4. Verify MD5 hash of chunk payload.
5. Write chunk to temp storage.
6. Insert `upload_task_chunks` row with `ON CONFLICT DO NOTHING` for idempotent repeated chunk uploads.

Source: `src/services/UploadService.cpp:368`.

### Complete upload

`UploadService::CompleteUpload` starts at `src/services/UploadService.cpp:550`.

Current behavior:

1. Validate upload task and idempotently return success if already completed.
2. Check chunk coverage.
3. Assemble chunks and verify final MD5 hash.
4. Look up existing content and filename collision.
5. If content already exists, delete assembled temp file and reuse content.
6. If content does not exist, promote assembled file to final storage and remember compensation is needed if DB work fails.
7. In a DB transaction:
   - Increment existing content ref-count or create `file_contents` with `ref_count = 1`.
   - Insert `files` row.
   - Transfer quota with `storage_reserved = GREATEST(storage_reserved - size, 0)` and `storage_used = storage_used + size`.
   - Mark upload task `status = 1`.
   - Delete chunk tracking rows.
8. If DB work fails after promotion, delete the promoted final blob as compensation.
9. Clean temporary upload artifacts.

Sources: `src/services/UploadService.cpp:550`, `src/services/UploadService.cpp:807`, `src/services/UploadService.cpp:862`, `src/services/UploadService.cpp:915`, `src/services/UploadService.cpp:946`.

### Cancel upload

`UploadService::CancelUpload` starts at `src/services/UploadService.cpp:983`.

Current behavior:

1. Validate task ownership.
2. Return success if task is already terminal.
3. Release reserved quota.
4. Mark upload task `status = 2`.
5. Delete chunk tracking rows.
6. Clean temp directory.

Source: `src/services/UploadService.cpp:983`.

Important current behavior: quota release occurs before the status update. If the status update fails after release, reserved quota and task state can drift.

### Expired upload cleanup

`CleanupService::CleanupExpiredUploadTasks` starts at `src/services/CleanupService.cpp:315`.

Current behavior:

1. Select `upload_tasks` where `status = 0` and `expires_at < NOW()`.
2. Clean temp storage for each task.
3. Mark selected tasks `status = 3` with fail reason `任务过期`.
4. Release accumulated `reserved_bytes` from `users.storage_reserved`.

Sources: `src/services/CleanupService.cpp:315`, `src/services/CleanupService.cpp:341`, `src/services/CleanupService.cpp:364`, `src/services/CleanupService.cpp:372`.

## Quota and Accounting Lifecycle

### `users.storage_reserved`

Confirmed update paths:

| Path | Effect | Source |
| --- | --- | --- |
| Normal upload init | `storage_reserved += file_size` if `storage_used + storage_reserved + file_size <= storage_quota` | `src/services/UploadService.cpp:1048` |
| Upload finalization | `storage_reserved = GREATEST(storage_reserved - file_size, 0)` | `src/services/UploadService.cpp:862` |
| Upload cancel | `storage_reserved = GREATEST(storage_reserved - reserved_bytes, 0)` | `src/services/UploadService.cpp:1003`, `src/services/UploadService.cpp:1077` |
| Expired upload cleanup | `storage_reserved = GREATEST(storage_reserved - accumulated_reserved_bytes, 0)` | `src/services/CleanupService.cpp:372` |

Potential gap: `InitUpload` inline expired-task cleanup removes expired tasks but does not visibly release reserved bytes.

### `users.storage_used`

Confirmed update paths:

| Path | Effect | Source |
| --- | --- | --- |
| Upload finalization | `storage_used += file_size` while reserved bytes are released | `src/services/UploadService.cpp:862` |
| Copy pre-check/reservation | `storage_used += total_copy_size` if quota allows | `src/services/FileMutationService.cpp:1176` |
| Copy partial release | `storage_used -= (total_copy_size - actual_copy_size)` if some planned copies were skipped | `src/services/FileMutationService.cpp:879` |
| Expired trash cleanup | `storage_used += delta`, where delta is negative accumulated item sizes | `src/services/CleanupService.cpp:293` |

`UpdateStorageUsed` swallows DB errors after logging them, so accounting drift is possible if a decrement fails. Sources: `src/services/UploadService.cpp:1308`, `src/services/FileMutationService.cpp:1208`.

### Accounting semantic tension

Current behavior is not a clean match for one obvious product rule:

| Operation | Physical unique-byte interpretation | Logical per-user-byte interpretation | Current behavior |
| --- | --- | --- | --- |
| Instant upload reusing content | no increase | should increase | no increase |
| Copy reusing content | should not increase | increase | increase |

This should remain an open product decision before extracting a `QuotaService` or `StorageAccountingService`.

## Content Ref-count Lifecycle

Confirmed increment paths:

| Path | Effect | Source |
| --- | --- | --- |
| Instant upload | `file_contents.ref_count = ref_count + 1` | `src/services/UploadService.cpp:147` |
| Complete upload dedup hit | `file_contents.ref_count = ref_count + 1` | `src/services/UploadService.cpp:818` |
| Copy file/folder | Increment content refs for copied files | `src/services/FileMutationService.cpp:413`, `src/services/FileMutationService.cpp:736` |

Confirmed decrement path:

| Path | Effect | Source |
| --- | --- | --- |
| Expired trash cleanup | decrement by count of expired trash references, clamped with `GREATEST(..., 0)` | `src/services/CleanupService.cpp:170` |

Blob deletion is guarded by zero-ref verification during trash cleanup. The cleanup transaction records zero-ref candidates, deletes trash rows, then re-queries `file_contents` for `ref_count = 0` before deleting physical blobs. Sources: `src/services/CleanupService.cpp:183`, `src/services/CleanupService.cpp:222`, `src/services/CleanupService.cpp:239`.

## Trash Lifecycle

`FileMutationService::Delete` starts at `src/services/FileMutationService.cpp:904`.

Current move-to-trash behavior:

1. Normalize requested file/folder ids.
2. Fetch folder delete plans and avoid duplicate file handling for files covered by selected folders.
3. Build trash records for explicit files and top-level folders.
4. Insert trash rows.
5. Remove share links for deleted files/folders and cancel shares that become empty.
6. Delete active `files` and `folders` rows.
7. Do not decrement `file_contents.ref_count` immediately.
8. Do not decrease `users.storage_used` immediately.

Expired trash cleanup behavior:

1. Fetch expired trash rows in batches.
2. Resolve content ids for file trash items.
3. Decrement content ref-counts.
4. Delete trash rows.
5. Accumulate negative storage deltas per user.
6. Re-verify zero-ref content rows and delete blobs through storage manager.
7. Apply `users.storage_used` decrements after chunk processing.

Sources: `src/services/FileMutationService.cpp:904`, `src/services/FileMutationService.cpp:985`, `src/services/FileMutationService.cpp:1107`, `src/services/CleanupService.cpp:48`, `src/services/CleanupService.cpp:170`, `src/services/CleanupService.cpp:193`, `src/services/CleanupService.cpp:239`, `src/services/CleanupService.cpp:293`.

Current quota rule: trashed items continue to count against `storage_used` until permanent deletion or expiration cleanup.

## Physical Storage Deletion Paths

Confirmed temp-file cleanup paths:

| Path | Storage effect | Source |
| --- | --- | --- |
| Cancel upload | `CleanupTemp(upload_id)` | `src/services/UploadService.cpp:1030` |
| Complete upload success | `CleanupTemp(upload_id)` | `src/services/UploadService.cpp:946` |
| Expired upload cleanup | `CleanupTemp(task_id)` | `src/services/CleanupService.cpp:341` |
| InitUpload finds expired resumable task | `CleanupTemp(task_id)` | `src/services/UploadService.cpp:247` |

Confirmed assembled/final blob deletion paths:

| Path | Storage effect | Source |
| --- | --- | --- |
| Complete upload hash mismatch | delete assembled path | `src/services/UploadService.cpp:677` |
| Complete upload duplicate filename | delete assembled path | `src/services/UploadService.cpp:734` |
| Complete upload dedup hit | delete assembled path | `src/services/UploadService.cpp:766` |
| Complete upload promote failure | cleanup assembled path | `src/services/UploadService.cpp:774` |
| DB failure after final promotion | delete promoted final blob as compensation | `src/services/UploadService.cpp:915` |
| Expired trash cleanup with verified zero-ref content | delete content `storage_path` | `src/services/CleanupService.cpp:239` |

## Download Metadata Side Effects

### Private download

`FileController::Download` calls `FileQueryService::GetDownloadData` and then `BuildDownloadResponse`.

Current behavior:

- Reads file and content metadata.
- Returns a range-capable download response.
- Does not update `files.download_count`.
- Does not update `files.last_accessed_at`.

Sources: `src/controllers/FileController.cpp:313`, `src/services/FileQueryService.cpp:355`.

### Private download info

`FileQueryService::GetDownloadInfo` reads file/content metadata and returns response data. It does not update `files.download_count` or `files.last_accessed_at`.

Source: `src/services/FileQueryService.cpp:307`.

### Share download

`ShareController::Download` calls `ShareService::GetDownloadInfo`, builds the download response, then increments share download count.

Current behavior:

- Validates share token and requested share id.
- Validates share status, expiration, membership, and `permission == "download"` in `ShareService::GetDownloadInfo`.
- Returns a range-capable download response.
- Updates `shares.download_count` through `ShareService::IncrementDownloadCount`.
- Does not update `files.download_count`.
- Does not update `files.last_accessed_at`.

Sources: `src/controllers/ShareController.cpp:339`, `src/services/ShareService.cpp:864`, `src/services/ShareService.cpp:1607`.

### Share download info

`ShareController::DownloadInfo` calls `ShareService::GetDownloadInfo` and returns metadata. It does not increment `shares.download_count`.

Source: `src/controllers/ShareController.cpp:290`.

## Behavior-preserving Constraints for Later Refactors

Later refactors should preserve the following unless a separate proposal intentionally changes behavior:

- Public API response envelopes and route shapes remain unchanged.
- Public auth, health, and public share exemptions remain reachable without JWT where currently configured.
- Rate-limit Redis failures remain fail-open unless a behavior-change proposal decides otherwise.
- Upload finalization transfers reserved bytes to used bytes in the same DB transaction as file/content creation and task finalization.
- Trash rows retain logical file content references until permanent deletion/expiration cleanup.
- Physical blob deletion remains guarded by zero-ref verification.
- Private downloads do not update file download metadata.
- Share downloads increment share-level download count, not file-level download count.

## Open Questions / Follow-ups

- Should `storage_used` mean physical unique bytes or logical per-user bytes?
- Should instant upload increase `storage_used` if copy does?
- Should copy accounting use a reservation-style model instead of incrementing `storage_used` before copy work completes?
- Should `InitUpload` expired-task cleanup release reserved quota before deleting an expired task?
- Should `CancelUpload` mark terminal state before releasing reserved quota, or should both happen in a transaction-like helper?
- Should private downloads update `files.download_count` and `files.last_accessed_at`?
- Should share downloads also update file-level metadata, or only share-level metadata?

## Validation Notes

Existing unit tests cover individual filters and path predicates, including:

- `test/filters/AdminAuthFilter_test.cpp`
- `test/filters/DownloadRateLimit_test.cpp`
- `test/filters/RegisterRateLimit_test.cpp`
- `test/filters/ShareAuthFilter_test.cpp`
- `test/filters/ShareRateLimitFilter_test.cpp`
- `test/filters/FilterOwnership_test.cpp`
- `test/filters/AdminRateLimit_test.cpp`
- `test/filters/FolderRateLimit_test.cpp`

Latest validation of the current filter behavior (2026-07-19):

```text
cmake --build --preset linux-debug-clang
./build/linux-debug-clang/test/disk-test \
  --gtest_filter='ConfigMgrShareRateLimitTest.*:RedisKeyPrefix.BuildShare*:ShareAuthFilterTest.*:ShareAuthFilterRuntimeTest.*:ShareRateLimitFilterTest.*:FilterOwnershipTest.*:RedisServiceRuntimeTest.IncrWithExpire*' \
  --gtest_color=no
ctest --preset linux-debug-clang --output-on-failure
```

Result: the focused suite passed 55/55. The full backend run passed all 1,179 enabled non-gated tests; the two explicit S3 environment gates were skipped, for 1,181 total entries and zero failures. `ShareRateLimitIntegration` passed all ten evidence IDs in that serial run.
