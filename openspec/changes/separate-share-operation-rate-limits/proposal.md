## Why

Public share access, browse, and download operations currently consume one
IP-keyed limiter even though they have different sensitivity and authenticated
identity. This allows one operation to exhaust unrelated traffic and prevents
download limits from following an issued Share Token across client addresses.

## What Changes

- Replace the shared public-share limiter with independent fixed-window access,
  browse, and download limiter families.
- Keep access keyed by normalized client IP, and key browse and download by the
  `jti` of a successfully verified and scope-authorized Share Token.
- Apply authenticated operation limiters after Share Token authentication;
  download metadata, content requests, Range/retry requests, and save-to-drive
  consume one shared per-JTI download bucket.
- Preserve the standard HTTP 429 response, limiter headers, fixed-window expiry,
  and fail-open behavior when Redis limiter state is unavailable.
- **BREAKING** Replace the two `share_public_rate_limit_*` settings with six
  operation-specific settings. The removed settings have no runtime aliases.
- Add executable unit, ownership, integration, isolation, response, failure, and
  credential-exclusion evidence for the three limiter families.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sharing`: Define operation coverage, identities, limits, authentication
  precedence, isolation, fixed-window behavior, and responses for share access,
  browse, and download rate limits.
- `request-filter-application`: Assign each share limiter to one route-owned path
  and require authenticated operation limiters to execute after Share Token
  authentication and scope authorization.
- `runtime-configuration`: Replace the public-share limiter settings with access,
  browse, and download limit/window settings and documented defaults.
- `validation-and-performance`: Require operation-specific automated evidence for
  limiter boundaries, isolation, authentication precedence, Redis failure, and
  secret exclusion.

## Impact

- Affected routes: share access, browse, download metadata, download content, and
  save-to-drive. Route paths and successful response schemas do not change, but
  browse, download, and save may now return the existing standard 429 response.
- Affected backend areas: `ShareController`, Share Token authentication,
  route-owned filters, rate-limit helpers, `ConfigMgr`, `RedisKeyPrefix`, Drogon
  global-filter configuration, focused GoogleTest suites, and serial integration
  tests.
- Operators must migrate to the six new configuration keys before deployment.
  Existing Redis public-share keys are obsolete and may expire naturally.
- No database schema, dependency, owner-token format, or client implementation
  change is required.
