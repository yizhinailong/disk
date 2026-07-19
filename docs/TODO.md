# Backend TODO

> Updated: 2026-07-19
>
> This file tracks verified, currently unfinished backend work. Active work uses
> checkboxes. Deferred client work is retained only as a non-blocking parking lot
> without checkboxes and must not be counted as backend completion work.
>
> Completed implementation checklists and their evidence belong in `docs/archive/`.
> Once an active item is complete, move its durable evidence to an archive note and
> remove the completed checklist from this file rather than leaving checked history.

## Current Baseline

- The completed backend refactor roadmap is archived at
  [`docs/archive/2026-07-14-backend-refactor-todo.md`](archive/2026-07-14-backend-refactor-todo.md).
- The self-contained backend CTest closure is archived at
  [`docs/archive/2026-07-18-ctest-self-contained.md`](archive/2026-07-18-ctest-self-contained.md).
- The backend implementation-marker audit is archived at
  [`docs/archive/2026-07-18-backend-implementation-marker-audit.md`](archive/2026-07-18-backend-implementation-marker-audit.md).
- `openspec validate --all --strict --no-interactive` currently validates 23 of 25
  items. The only accepted aggregate failures are the deferred `TBD.` Purpose text
  in `web-client-experience` and `desktop-client-experience`. No additional failure
  is an acceptable backend baseline.

## Working Rules

- Propose requirement changes in the relevant OpenSpec capability before updating
  legacy narrative references or runtime behavior.
- Update the relevant API, design, deployment, and test authority documents before
  changing behavior. Git history must not place behavioral changes before their
  governing contract.
- Every active item must link its relevant OpenSpec requirement, `docs/design/`
  source, accepted decision, or executable evidence.
- Mark an implementation item complete only when the documented behavior,
  implementation, and proportionate executable tests all exist.
- Treat an environment-gated test as verified only when it actually runs. A skip is
  not a pass for the gated behavior.
- Keep Redis rate-limit failures fail-open unless an approved endpoint-specific
  OpenSpec change explicitly chooses another policy.
- Never store or log passwords, password hashes, raw Share Tokens, authorization
  headers, or other replayable credentials in Redis keys, application logs, audit
  records, test evidence, or fixtures committed to the repository.
- Do not update `clients/`, `docs/desktop/`, or client-only OpenSpec requirements
  during this backend phase unless a backend contract change requires a narrowly
  scoped compatibility note.
- Do not preserve obsolete filters, configuration aliases, Redis key builders, or
  tests as parallel compatibility paths when the existing path can be replaced.

---

## P0 - Backend Completion

### P0.4 Separate sensitive share-operation rate limits

The Share Token scope, live-state revocation, password-failure protection, and
audit contracts are complete. The remaining backend security gap is the shared
IP-based public-share limiter in `SharePublicRateLimitFilter`.

#### Authority and traceability

- Normative share behavior: [`openspec/specs/sharing/spec.md`](../openspec/specs/sharing/spec.md)
- Filter ownership and fail-open policy: [`openspec/specs/request-filter-application/spec.md`](../openspec/specs/request-filter-application/spec.md)
- Runtime configuration policy: [`openspec/specs/runtime-configuration/spec.md`](../openspec/specs/runtime-configuration/spec.md)
- Validation policy: [`openspec/specs/validation-and-performance/spec.md`](../openspec/specs/validation-and-performance/spec.md)
- API security contract: [`docs/design/02-API接口设计.md` section 9.4.3 and 9.4.5](design/02-API接口设计.md)
- Executable test authority: [`docs/design/04-系统测试计划.md`](design/04-系统测试计划.md)
- Unit-test reference: [`docs/design/06-单元测试用例.md`](design/06-单元测试用例.md)
- Deployment/configuration reference: [`docs/design/05-部署运维指南.md`](design/05-部署运维指南.md)
- Current filter baseline: [`src/filters/SharePublicRateLimitFilter.cpp`](../src/filters/SharePublicRateLimitFilter.cpp)
- Current filter ownership characterization: [`test/filters/FilterOwnership_test.cpp`](../test/filters/FilterOwnership_test.cpp)

#### Target contract to formalize

All operation limits use a fixed window. The first request creates the Redis key
with the configured TTL; later increments do not refresh that expiry.

| Limiter | Covered routes | Default limit | Key identity | Required filter order |
|---------|----------------|---------------|--------------|-----------------------|
| Share access | `POST /api/share/access/{share_id}` | 30 requests / 60 seconds | Normalized client IP | Access limiter; no owner JWT or Share Token is required |
| Share browse | `GET /api/share/browse/{share_id}` | 60 requests / 60 seconds | JTI from a verified Share Token | `ShareAuthFilter`, then browse limiter |
| Share download | Download metadata, binary content, and save-to-drive routes | 10 requests / 60 seconds | JTI from a verified Share Token | `ShareAuthFilter`, then download limiter |

The share-download bucket covers all of these requests and they consume the same
per-JTI counter:

- `GET /api/share/download/{share_id}/{file_id}/info`
- `GET /api/share/download/{share_id}/{file_id}`
- `POST /api/share/save/{share_id}`
- Every binary download HTTP request, including Range resume and automatic retry
  requests; one logical file download can therefore consume more than one request.

Additional contract rules:

- General share-access limiting is independent of the existing
  `rate:share_password:{share_code}:{normalized_ip}` failed-validation counter.
  Successful and rejected access requests consume the general access bucket;
  countable password failures additionally consume the password-failure bucket.
- Browse and download counters use the verified JWT `jti` claim. They must not hash,
  persist, or log the raw `X-Share-Token` value.
- Missing, malformed, expired, revoked, or insufficient-scope Share Tokens are
  rejected by `ShareAuthFilter` before an authenticated operation bucket is
  incremented. Live share-state and route/share binding checks retain their existing
  service-layer behavior unless the OpenSpec change explicitly approves a separate
  change.
- Save-to-drive remains protected by the global owner JWT path in addition to Share
  Token authentication and consumes the share-download bucket after both required
  authentication contexts are available.
- Exceeding a bucket returns HTTP 429 with code `10005` and the shared
  `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and
  `Retry-After` headers.
- Redis increment/check failure is logged without credential material and allows
  the request to continue.
- The target Redis key families are:
  `rate:share_access:{normalized_ip}:{window}`,
  `rate:share_browse:{jti}:{window}`, and
  `rate:share_download:{jti}:{window}`.
- The target runtime configuration keys and defaults are:
  `share_access_rate_limit_per_minute=30`,
  `share_access_rate_limit_window_seconds=60`,
  `share_browse_rate_limit_per_minute=60`,
  `share_browse_rate_limit_window_seconds=60`,
  `share_download_rate_limit_per_minute=10`, and
  `share_download_rate_limit_window_seconds=60`.
- The obsolete `rate:share_public:{ip}:{window}` family and the single
  `share_public_rate_limit_*` configuration contract are removed without runtime
  aliases.

#### 1. Specification and authority documents

- [x] Create an OpenSpec change named `separate-share-operation-rate-limits` before
  changing runtime behavior.
- [x] Add the complete operation-specific contract and scenarios to `sharing`,
  including access, browse, download metadata/content, Range/retry counting,
  save-to-drive, authentication precedence, bucket isolation, and safe JTI keying.
- [x] Update `request-filter-application` so the access limiter and authenticated
  share-operation limiter ownership/order are normative and each request is counted
  exactly once.
- [x] Update `runtime-configuration` from one public-share family to the three
  access, browse, and download configuration families, including defaults and
  invalid-value fallback behavior.
- [x] Update `validation-and-performance` with operation-specific share rate-limit
  validation rather than the legacy single share-access case.
- [x] Update API section 9.4.3 with the exact table above and keep the section 9.4.5
  checklist item unchecked until executable evidence exists.
- [x] Add focused system-test cases to `docs/design/04-系统测试计划.md`; change the
  share-module summary so it does not claim complete rate-limit coverage early.
- [x] Update `docs/design/06-单元测试用例.md` so its inventory and commands include
  the new Share Auth, configuration, Redis-key, limiter, and ownership coverage.
- [x] Document the new configuration keys, defaults, old-key removal, Redis key
  families, response contract, and fail-open policy in the deployment guide.
- [x] Record whether the new 429 surfaces require a client compatibility note. Do
  not expand this backend change into client implementation work without evidence.

#### 2. Runtime implementation

- [x] Replace `share_public_rate_limit_per_minute` and
  `share_public_rate_limit_window_seconds` with the six named access, browse, and
  download settings above in `config.json` and `ConfigMgr`.
- [ ] Add `RedisKeyPrefix` builders for all three key families, using normalized IP
  for access and verified JTI for browse/download. Remove ad hoc key construction.
- [ ] Expose the verified Share Token JTI to downstream route filters through a
  narrowly named request attribute only after full Share Token verification and
  scope validation succeed.
- [ ] Replace the global `SharePublicRateLimitFilter` path with route-owned
  operation-specific limiting. Remove it from `GlobalFilters`; do not keep a second
  global copy.
- [ ] Attach the access limiter to the access route and attach authenticated
  operation limiting after `ShareAuthFilter` on browse, download metadata, download
  content, and save-to-drive routes.
- [ ] Preserve shared fixed-window mechanics, standard 429 construction, and
  fail-open Redis behavior through the existing rate-limit helpers.
- [ ] Remove obsolete source declarations, CMake entries, getters, constants,
  key patterns, cleanup helpers, and tests that only describe the shared public IP
  bucket.
- [ ] Ensure application logs expose operation type and non-secret diagnostics but
  never the raw token, token header, or password material.

#### 3. Executable coverage

- [ ] Add unit coverage for all new configuration getters, documented defaults,
  valid configured values, and absent/zero/negative fallback behavior.
- [ ] Add `RedisKeyPrefix` tests for IPv4, IPv6, port normalization, JTI separation,
  operation separation, and absence of raw-token input.
- [ ] Add Share Auth/filter tests proving JTI is exposed only after successful
  verification and that the limiter executes after the required authentication and
  scope checks.
- [ ] Update filter ownership tests to prove each limiter has one owner, the old
  global filter is absent, and Redis failures remain fail-open.
- [ ] Add and register a serial integration test such as
  `test/integration/test_share_rate_limit.py` in `test/CMakeLists.txt`.
- [ ] Update existing share-password integration cleanup so it removes only the
  new access keys it owns and cannot hide browse/download counter leakage.

The executable matrix must prove all of the following, using configured values
rather than hard-coded test assumptions where practical:

| Evidence ID | Required proof |
|-------------|----------------|
| `SHARE-RATE-ACCESS-001` | Against an active no-password share, the first 30 access requests from one normalized IP are not throttled and request 31 returns HTTP 429 / `10005`. |
| `SHARE-RATE-BROWSE-001` | The first 60 authenticated browse requests for one JTI are not throttled and request 61 is throttled. |
| `SHARE-RATE-DOWNLOAD-001` | Download metadata, content, and save-to-drive share one 10-request JTI bucket; request 11 through any covered route is throttled. |
| `SHARE-RATE-RANGE-001` | Range resume and retry HTTP requests consume the download bucket exactly once per request. |
| `SHARE-RATE-ISOLATION-001` | Access, browse, and download buckets do not consume one another, and separately issued token JTIs do not consume one another. |
| `SHARE-RATE-AUTH-001` | Missing/invalid tokens and scope-denied operations retain their authentication/authorization response and do not consume authenticated buckets. |
| `SHARE-RATE-CONFIG-001` | Valid runtime values are honored and absent/invalid values use the documented defaults. |
| `SHARE-RATE-RESPONSE-001` | Every limiter returns the standard 429 body and all four rate-limit headers at the boundary. |
| `SHARE-RATE-REDIS-001` | Redis limiter failure is observable but fail-open and does not replace the underlying business response. |
| `SHARE-RATE-SECRETS-001` | Redis keys, application logs, audit rows, and saved test evidence contain no raw Share Token or authentication header. |

#### 4. Verification and closure

- [ ] Strictly validate the OpenSpec change and each affected capability before
  implementation, and repeat validation after implementation.
- [ ] Run `clang-format` on every changed C++ source/test file and review the final
  diff for unrelated formatting churn.
- [ ] Run focused GoogleTest/CTest coverage for Share Auth, share rate limiting,
  Redis key prefixes, configuration, and filter ownership.
- [ ] Run the new share-rate integration scenario through CTest from the same serial
  harness used by the backend suite.
- [ ] Run `cmake --build --preset linux-debug-clang` successfully.
- [ ] From an initially stopped backend, run the full backend CTest suite and require
  all enabled non-gated tests to pass. Explicit environment gates may be reported as
  skipped but are not evidence that their gated behavior passed.
- [ ] Run aggregate strict OpenSpec validation and confirm it either passes fully or
  differs from full success only by the two recorded deferred client Purpose
  placeholders.
- [ ] Search the final tree for the obsolete `rate:share_public`,
  `share_public_rate_limit`, and `SharePublicRateLimitFilter` identifiers; any
  remaining occurrence must be an intentional historical reference.
- [ ] Check API section 9.4.5 only after the implementation and the evidence matrix
  pass.
- [ ] Archive the completed OpenSpec change, create a dated completion/evidence note
  under `docs/archive/`, and remove this completed P0 checklist from the active TODO.

---

## Deferred Client Work

These entries are deliberately inactive, have no checkboxes, and do not block the
backend P0. They must be revalidated against the then-current client trees before a
new client change is proposed.

### Web validation and test infrastructure

- `WEB-DEFER-001` - Replace hard-coded identities in
  `clients/disk-web/e2e/fixtures.ts` with seeded fixtures or environment-provided
  identities. Completion requires isolated setup/teardown and a documented command
  that can run Playwright from a clean backend data state.
- `WEB-DEFER-002` - Run real-browser verification for folder-tree synchronization
  after create, rename, move, delete, and navigation. Store-level hierarchy refresh
  and unit coverage already exist; the remaining gap is browser/backend integration
  and stable E2E coverage for the observable tree, list, and breadcrumb result.

### Desktop validation

- `DESKTOP-DEFER-001` - Run a real-backend visitor download scenario covering a
  partial file plus persisted resume state, a 206 response, final size verification,
  and final hash verification. `TransferManager` unit tests already cover Range,
  restart, size mismatch, and hash mismatch; the remaining gap is end-to-end runtime
  evidence against a shared file.

### Client documentation hygiene

- `CLIENT-DOC-DEFER-001` - Replace the `TBD.` Purpose text in
  `web-client-experience` and `desktop-client-experience`. Completion requires both
  specs and aggregate `openspec validate --all --strict --no-interactive` to pass.
- `DESKTOP-DOC-DEFER-002` - Re-audit DOC-00 through DOC-06 against the split QML
  component structure, correct stale anchors/status labels, and deduplicate
  overlapping `[规划]` entries. The audit must record file-level evidence rather than
  converting unverified plans into implemented claims.

### Desktop product candidates requiring acceptance

The following are candidates, not accepted implementation tasks: visible owner-file
pagination controls or load-more behavior, list/grid layout switching, internal
drag/drop movement, external drag/drop upload policy, and loading skeletons. Each
candidate requires an explicit product decision and OpenSpec proposal before code;
partial manager signals, properties, or documentation mentions are not acceptance
evidence. Accepted work must include corresponding Qt Test or Qt Quick Test coverage.

---

## Definition of Done for Active Backend Work

- [ ] All normative and supporting authority documents are synchronized before the
  first behavioral commit.
- [ ] Every changed backend behavior has focused unit and serial integration coverage
  proportional to its security and compatibility risk.
- [ ] Runtime configuration, Redis keys, filter ownership, failure policy, response
  shape, and credential-exclusion rules match the accepted contract.
- [ ] Focused verification, the Linux debug build, and the full self-contained CTest
  run satisfy the P0.4 closure gates above.
- [ ] The OpenSpec change and affected capabilities pass strict validation, with no
  new aggregate failure hidden by the two recorded deferred client placeholders.
- [ ] Completed work and evidence are archived, completed checklists are removed,
  and this file contains only verified unfinished backend work plus the explicitly
  non-blocking deferred client parking lot.
