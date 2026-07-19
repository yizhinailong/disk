# Share Operation Rate-limit Closure

**Completed:** 2026-07-19
**OpenSpec change:** `separate-share-operation-rate-limits`

## Outcome

The former global IP-based public-share limiter was replaced by route-owned fixed-window limiters. Public share access uses a normalized client-IP bucket. Browse and download operations run only after Share Token authentication and use the verified token JTI; the raw token is never accepted as key input. Download metadata, binary content, Range/retry requests, and save-to-drive share one download bucket. Save-to-drive still requires the owner JWT in addition to the Share Token.

The default contracts are access 30 requests per 60 seconds, browse 60 per 60 seconds, and download 10 per 60 seconds. Every exceeded bucket returns HTTP 429, code `10005`, and the four shared rate-limit headers. Redis counter failures remain observable and fail open. The obsolete `SharePublicRateLimitFilter`, `rate:share_public` key family, and `share_public_rate_limit_*` runtime settings have no compatibility aliases.

## Integration Evidence

The serial `ShareRateLimitIntegration` produced the following ten redacted evidence records in `.sisyphus/evidence/share-rate-limit-summary.json`. That generated file is intentionally ignored; this note is the durable result record.

| Evidence ID | Verified result |
| --- | --- |
| `SHARE-RATE-ACCESS-001` | Requests 1-30 from one normalized IP were allowed; request 31 returned the standard 429 response. |
| `SHARE-RATE-BROWSE-001` | Requests 1-60 for one verified JTI were allowed; request 61 was limited. |
| `SHARE-RATE-DOWNLOAD-001` | Metadata, content, Range/retry, and save consumed one 10-request JTI bucket; request 11 was limited. |
| `SHARE-RATE-RANGE-001` | Initial, Range, and retry HTTP requests each incremented the download counter exactly once. |
| `SHARE-RATE-ISOLATION-001` | Access, browse, and download buckets were isolated; independently issued JTIs did not share counters. |
| `SHARE-RATE-AUTH-001` | Missing, invalid, expired, revoked, scope-denied, and missing-owner-auth cases preserved their auth response and created no authenticated counter. |
| `SHARE-RATE-CONFIG-001` | Explicit positive values loaded; absent, zero, negative, wrong-type, and legacy settings used documented defaults. |
| `SHARE-RATE-RESPONSE-001` | All three boundaries returned HTTP 429 / `10005` / `Too many requests` with limit, remaining, reset, and retry headers. |
| `SHARE-RATE-REDIS-001` | Both limiters continued on injected Redis failure; the fixed-window TTL was set only on first increment. |
| `SHARE-RATE-SECRETS-001` | Redis keys, application logs, audit rows, and saved evidence contained no raw token, auth header, password, or password hash. |

## Verification

- `openspec validate separate-share-operation-rate-limits --strict --no-interactive` passed before closure.
- Changed C++ lines passed `git clang-format --diff origin/main HEAD`; no unrelated whole-file formatting churn was introduced.
- The focused configuration, Redis-key, Share Auth, share limiter, ownership, and fixed-window suite passed 55/55.
- `ShareRateLimitIntegration` passed all ten evidence IDs; the standalone run took 20.41 seconds and the scenario also passed in the full serial CTest run.
- `cmake --build --preset linux-debug-clang` passed.
- From an initially stopped backend, `ctest --preset linux-debug-clang --output-on-failure` completed 1,181 entries in 168.10 seconds: 1,179 enabled tests passed, zero failed, and the two explicit S3 environment gates were skipped. Those skips are not evidence for gated S3 behavior.
- Aggregate strict OpenSpec validation had the accepted 23/25 result before archival and 22/24 after the completed active change left the validation set. In both runs, the only failures were the deferred `TBD.` Purpose fields in `web-client-experience` and `desktop-client-experience`.
- The final obsolete-identifier scan found no runtime use. Remaining names occur only in migration/history documentation, negative compatibility tests, and ownership assertions that prove the obsolete files and declarations are absent.

The governing API, deployment, unit-test, and system-test documents were synchronized before runtime changes. API section 9.4.5 was completed only after this evidence matrix and the full backend verification passed.
