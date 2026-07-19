## Context

`SharePublicRateLimitFilter` is currently a global, path-aware filter that counts
share access, browse, and download requests in one
`rate:share_public:{ip}:{window}` bucket. It runs without a verified Share Token,
does not cover save-to-drive, and cannot distinguish the sensitivity or caller
identity of authenticated share operations.

Share Token verification already returns a validated `jti`, share identifier, and
scope. `ShareAuthFilter` enforces token validity, Redis-backed token revocation, and
the operation's scope before the controller runs. Save-to-drive additionally passes
through the global owner JWT filter because it is not a public JWT exemption.

The existing `RateLimitHelper` and `RedisService::IncrWithExpire` implement the
standard response and an atomic fixed window whose TTL is set only when the counter
is first created. The change must reuse those mechanics, preserve Redis fail-open
behavior, and avoid raw credentials in keys, logs, audit rows, fixtures, or evidence.

## Goals / Non-Goals

**Goals:**

- Enforce independent access, browse, and download request budgets with the exact
  documented defaults and identities.
- Count authenticated share operations only after successful Share Token
  verification and operation-scope authorization.
- Make route/filter ownership explicit so every covered HTTP request increments
  exactly one operation counter exactly once.
- Provide executable boundary, isolation, failure, response, and credential-safety
  evidence through focused C++ tests and one serial integration scenario.
- Remove the obsolete global filter, configuration keys, Redis key construction,
  build entries, tests, and cleanup paths instead of retaining aliases.

**Non-Goals:**

- No sliding window, queueing, token-bucket smoothing, or distributed quota beyond
  the existing Redis fixed-window counter.
- No change to share-token format, expiry, live share-state checks, password-failure
  limiting, successful API schemas, or database schema.
- No attempt to combine multiple Range requests into one logical-download charge;
  rate limiting remains request based.
- No client implementation work. Clients continue to receive the existing standard
  429 envelope and headers, with an API compatibility note for the new surfaces.

## Decisions

### Decision: use one access filter and one authenticated operation filter

`ShareAccessRateLimitFilter` will be attached only to
`POST /api/share/access/{share_id}`. `ShareOperationRateLimitFilter` will be attached
after `ShareAuthFilter` to browse, download metadata, download content, and
save-to-drive routes. It selects the browse family only for the browse route; every
other attached route uses the shared download family.

This keeps ownership visible in `ShareController` without duplicating identical
authenticated JTI extraction, fixed-window counting, response, and failure logic in
separate browse and download classes. The operation filter is never global and is
not attached outside the explicitly covered routes.

Alternative considered: three independent filter classes. Rejected because browse
and download differ only in configuration and key prefix, while route declarations
and tests can prove the single operation filter's exact attachment and ordering.

### Decision: publish JTI only after complete filter authorization

After `VerifyShareTokenWithRedis` succeeds and the requested path passes scope
authorization, `ShareAuthFilter` will add the validated JTI to the request attribute
`share_token_jti`. The authenticated rate limiter reads only that attribute and does
not parse, hash, store, or log `X-Share-Token`.

This makes authentication precedence structural: missing, malformed, expired,
revoked, or insufficient-scope tokens return their existing auth result before the
operation filter executes or any authenticated counter changes. Existing
`share_code` and internal `share_id` attributes remain unchanged for controllers.

Alternative considered: re-verify or hash the header in the rate-limit filter.
Rejected because it duplicates security logic, risks inconsistent error mapping,
and handles replayable token material in an additional component.

### Decision: keep live share-state checks in the service layer

The new limiter does not move route/share binding, current share permission,
cancelled status, or expiry checks out of `ShareService`. A structurally valid and
scope-authorized token can therefore consume an operation request before a later
live-state business rejection, matching the existing service boundary.

Alternative considered: move live database validation ahead of rate limiting.
Rejected because that would add database work before abuse control and broaden this
change into a share authorization redesign.

### Decision: use three centralized Redis key builders

`RedisKeyPrefix` will build these complete fixed-window keys:

- `rate:share_access:{normalized_ip}:{window}`
- `rate:share_browse:{jti}:{window}`
- `rate:share_download:{jti}:{window}`

The access builder owns IP normalization. Authenticated builders accept only the
verified JTI and window timestamp. No builder accepts a raw Share Token. Including
the fixed-window start in each key isolates windows while `IncrWithExpire` ensures
the first increment sets TTL and later increments do not refresh it.

Alternative considered: retain ad hoc string concatenation in filters. Rejected
because centralized builders make identity, operation separation, IPv4/IPv6
normalization, and secret exclusion directly testable.

### Decision: replace configuration without aliases

`ConfigMgr` and `config.json` will expose only these positive integer settings:

| Setting | Default |
|---------|---------|
| `share_access_rate_limit_per_minute` | 30 |
| `share_access_rate_limit_window_seconds` | 60 |
| `share_browse_rate_limit_per_minute` | 60 |
| `share_browse_rate_limit_window_seconds` | 60 |
| `share_download_rate_limit_per_minute` | 10 |
| `share_download_rate_limit_window_seconds` | 60 |

Missing, zero, negative, or non-integer-compatible values resolve to the documented
default. The old `share_public_rate_limit_per_minute` and
`share_public_rate_limit_window_seconds` keys are ignored and have no fallback
alias, so operators receive one unambiguous contract.

Alternative considered: read the old settings when new keys are absent. Rejected
because the accepted backlog explicitly prohibits parallel compatibility paths and
an alias would keep all operations coupled.

### Decision: preserve request-based download charging

Download metadata, binary content, and save-to-drive share one per-JTI counter.
Every HTTP content request passes the filter once, so initial downloads, Range
resume requests, and automatic retries each consume one request. The limiter does
not inspect Range values or attempt logical transfer correlation.

This matches the configured unit of requests per window and avoids persistent
transfer identity state. Clients can calculate retry timing from the standard
`Retry-After` and `X-RateLimit-Reset` headers.

### Decision: preserve the standard response and Redis fail-open boundary

All three families use `CheckFixedWindowLimit` and
`BuildRateLimitExceededResponse`. A count above the configured limit returns HTTP
429, business code `10005`, and `X-RateLimit-Limit`,
`X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After` headers.

Redis counter errors are logged with operation type and non-secret identity context
and return `nullptr` from the filter so the underlying handler continues. Focused
tests will cover this branch through the smallest injectable counter boundary
compatible with the existing default-constructed Drogon filters.

Alternative considered: fail closed when Redis is unavailable. Rejected because it
would make cache availability a public API availability dependency and contradicts
the existing runtime configuration contract.

## Risks / Trade-offs

- [Risk] A filter-order regression could expose a missing JTI to the operation
  limiter. -> Mitigation: declare `ShareAuthFilter` first on every authenticated
  route and add ownership/order tests plus successful-auth attribute tests.
- [Risk] One token can be shared across IPs and all users of that token consume the
  same authenticated budget. -> Mitigation: this is intentional capability-based
  accounting; separately issued tokens have separate JTI buckets.
- [Risk] A browser may use multiple Range requests and reach the limit during one
  perceived download. -> Mitigation: document per-request charging and expose the
  standard retry headers; do not promise logical-download accounting.
- [Risk] Removing old configuration keys can silently restore defaults after an
  incomplete deployment migration. -> Mitigation: document the breaking migration,
  update the repository configuration atomically, and search the final tree for
  obsolete runtime identifiers.
- [Risk] JTI values are correlatable identifiers even though they are not replayable
  tokens. -> Mitigation: keep them only in Redis limiter keys/request attributes,
  avoid normal success logging, apply short limiter TTLs, and verify raw token
  absence separately.
- [Risk] Redis errors temporarily remove abuse protection. -> Mitigation: retain
  explicit error logging and executable fail-open evidence while preserving API
  availability.

## Migration Plan

1. Validate this change and all four delta capabilities strictly.
2. Update the API, system-test, unit-test, and deployment references before runtime
   changes, including the new 429 compatibility note.
3. Deploy the six new configuration keys with the backend version that removes the
   two old keys; no dual-read interval is supported.
4. Add key builders, JTI propagation, route-owned filters, and focused tests; remove
   the old global filter and all obsolete registrations in the same implementation
   sequence.
5. Run focused tests, the serial integration matrix, the Linux debug build, full
   self-contained CTest, strict OpenSpec validation, and obsolete-identifier scans.
6. Existing `rate:share_public:*` keys require no migration and expire naturally.

Rollback uses a Git revert of the runtime and configuration commits together. An
operator rolling back the binary must also restore the old two configuration keys;
the new operation-specific Redis keys can expire naturally.

## Open Questions

None.
