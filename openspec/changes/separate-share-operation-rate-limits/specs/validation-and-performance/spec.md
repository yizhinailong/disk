## ADDED Requirements

### Requirement: Share Operation Rate Limit Validation
Automated validation SHALL prove the configured boundaries, route coverage,
identity and operation isolation, authentication precedence, response contract,
fixed-window behavior, Redis failure policy, and credential exclusion for share
access, browse, and download rate limits.

#### Scenario: SHARE-RATE-ACCESS-001 access boundary is validated
- **WHEN** the share rate-limit integration scenario exercises an active no-password share from one normalized IP
- **THEN** evidence SHALL show that the configured number of access requests continue and the next request returns HTTP 429 with code `10005`

#### Scenario: SHARE-RATE-BROWSE-001 browse boundary is validated
- **WHEN** the scenario exercises authenticated browse with one verified JTI
- **THEN** evidence SHALL show that the configured number of browse requests continue and the next request is throttled

#### Scenario: SHARE-RATE-DOWNLOAD-001 shared download boundary is validated
- **WHEN** the scenario mixes download metadata, binary content, and save-to-drive for one JTI
- **THEN** evidence SHALL show that all covered routes consume one configured download bucket and the next request through any covered route is throttled

#### Scenario: SHARE-RATE-RANGE-001 request charging is validated
- **WHEN** the scenario sends Range resume and retry HTTP requests
- **THEN** evidence SHALL show that each request consumes the download bucket exactly once

#### Scenario: SHARE-RATE-ISOLATION-001 isolation is validated
- **WHEN** the scenario uses multiple operation families and separately issued token JTIs
- **THEN** evidence SHALL show that operation families and distinct JTIs do not consume one another's counters

#### Scenario: SHARE-RATE-AUTH-001 authentication precedence is validated
- **WHEN** the scenario sends missing, invalid, revoked, route-mismatched, or insufficient-scope tokens
- **THEN** evidence SHALL show the existing authentication or authorization responses and no authenticated counter consumption

#### Scenario: SHARE-RATE-CONFIG-001 configuration is validated
- **WHEN** focused tests load valid, absent, zero, and negative share operation settings
- **THEN** evidence SHALL show configured values or the documented per-family defaults as applicable

#### Scenario: SHARE-RATE-RESPONSE-001 limited responses are validated
- **WHEN** each share limiter crosses its configured boundary
- **THEN** evidence SHALL show HTTP 429, code `10005`, and all four standard rate-limit headers

#### Scenario: SHARE-RATE-REDIS-001 fail-open behavior is validated
- **WHEN** focused tests make Redis limiter accounting fail
- **THEN** evidence SHALL show observable non-secret failure diagnostics and continuation to the underlying business response

#### Scenario: SHARE-RATE-SECRETS-001 credential exclusion is validated
- **WHEN** the scenario inspects Redis keys, logs, audit rows, fixtures, and saved evidence produced by share limiting
- **THEN** evidence SHALL show no raw Share Token, authentication header, password, or password hash

#### Scenario: Share rate-limit scenario is part of the backend suite
- **WHEN** the standard backend CTest suite is configured
- **THEN** the share operation integration scenario SHALL be registered as a serial test and SHALL use the shared backend lifecycle harness
