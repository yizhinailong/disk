## 1. Authority Documentation

- [x] 1.1 Replace API section 9.4.3 with the exact access, browse, download, authentication-order, fixed-window, response-header, key, and configuration contracts while leaving section 9.4.5 incomplete
- [x] 1.2 Add the ten evidence IDs and serial integration expectations to the system test plan without claiming implementation coverage early
- [x] 1.3 Update the unit-test inventory and focused commands for Share Auth, share rate-limit filters, configuration, Redis keys, ownership, and fail-open coverage
- [x] 1.4 Document the six new settings, defaults, obsolete-key removal, Redis key families, fixed-window behavior, response contract, failure policy, rollout, and rollback in the deployment guide
- [x] 1.5 Record that the new 429 surfaces reuse the existing response contract and require a compatibility note but no client implementation change

## 2. Configuration and Redis Keys

- [x] 2.1 Replace the two public-share settings with six operation-specific settings in `config.json` and `ConfigMgr`, including positive-value normalization and documented defaults
- [ ] 2.2 Add centralized access, browse, and download fixed-window builders to `RedisKeyPrefix` and remove ad hoc public-share key construction
- [ ] 2.3 Add focused configuration tests for valid, absent, zero, and negative values and Redis key tests for IPv4, IPv6, windows, JTI isolation, operation isolation, and raw-token exclusion

## 3. Authentication and Route-Owned Filters

- [x] 3.1 Publish `share_token_jti` from `ShareAuthFilter` only after token verification and operation-scope authorization, with focused success and rejection tests
- [ ] 3.2 Implement the route-owned access limiter using normalized IP, access configuration, the access key builder, the shared fixed-window helper, standard 429 responses, and explicit Redis fail-open logging
- [ ] 3.3 Implement the route-owned authenticated operation limiter using only `share_token_jti`, browse/download configuration and keys, shared download coverage, standard responses, and explicit Redis fail-open logging
- [ ] 3.4 Attach filters in the required controller order, remove `SharePublicRateLimitFilter` from global configuration and build targets, and delete its obsolete source and declarations
- [ ] 3.5 Update ownership and focused filter tests to prove exact attachment, authentication precedence, boundary behavior, shared and isolated buckets, response headers, and executable fail-open continuation

## 4. Serial Integration Evidence

- [ ] 4.1 Add `test/integration/test_share_rate_limit.py` through the shared lifecycle helpers and register it as a serial CTest
- [ ] 4.2 Narrow share-password integration cleanup to its owned access/password keys so it cannot mask browse or download leakage
- [ ] 4.3 Execute and save evidence for `SHARE-RATE-ACCESS-001`, `SHARE-RATE-BROWSE-001`, `SHARE-RATE-DOWNLOAD-001`, `SHARE-RATE-RANGE-001`, and `SHARE-RATE-ISOLATION-001`
- [ ] 4.4 Execute and save evidence for `SHARE-RATE-AUTH-001`, `SHARE-RATE-CONFIG-001`, `SHARE-RATE-RESPONSE-001`, `SHARE-RATE-REDIS-001`, and `SHARE-RATE-SECRETS-001`

## 5. Verification and Closure

- [ ] 5.1 Strictly validate this change and each affected OpenSpec capability before and after implementation
- [ ] 5.2 Format all changed C++ files and pass the focused Share Auth, rate-limit, Redis key, configuration, Redis service, and ownership tests
- [ ] 5.3 Pass the serial share-rate integration test, Linux debug build, and full self-contained backend CTest run from an initially stopped server
- [ ] 5.4 Preserve the aggregate strict OpenSpec baseline and prove obsolete runtime identifiers and replayable credentials are absent from non-historical output
- [ ] 5.5 Complete API section 9.4.5 only after evidence passes, archive this change with a dated evidence note, and remove the completed P0.4 checklist from `docs/TODO.md`
