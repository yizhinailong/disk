# Backend TODO

> Updated: 2026-07-14
>
> This file tracks verified, currently open backend work only. Client implementation,
> client validation, and client-documentation synchronization are paused and listed
> without checkboxes under Deferred Client Work. Completed work must be removed from
> the active checklist or moved to `docs/archive/`; it must not remain here as a checked
> historical log.
>
> The completed backend refactor roadmap is archived at
> [`docs/archive/2026-07-14-backend-refactor-todo.md`](archive/2026-07-14-backend-refactor-todo.md).

## Working Rules

- Update the relevant design, API, product, or test authority document before changing behavior.
- Mark an item complete only when implementation and proportionate tests both exist.
- Treat an environment-gated test as verified only when it actually runs; a skip is not a pass for the gated behavior.
- Do not update `clients/`, `docs/desktop/`, or client-only OpenSpec requirements during this backend phase unless a backend contract change requires a compatibility note.

---

## P0 - Share Security Contract Closure

The canonical checklist in `docs/design/02-API接口设计.md` section 9.4.5 is still unchecked and mixes implemented behavior with real gaps. Reconcile the contract before changing runtime behavior.

### P0.1 Reconcile the security checklist

- [ ] Audit every item in API section 9.4.5 against code and executable tests; mark only proven items complete.
- [ ] Document the intended Share Token `scope` values and their mapping to share permissions and visitor operations.
- [ ] Add a `scope` claim to generated Share Tokens, validate it, expose it through `ShareTokenClaims`, and add positive/negative tests.
- [ ] Confirm every visitor browse, download, metadata, and save-to-drive path revalidates share status and expiry after token verification; add missing coverage before checking the item off.
- [ ] Document cancellation semantics: current tokens may become unusable through share-status validation even when their individual hashes are not added to the Redis blacklist.

### P0.2 Correct password protection semantics

- [ ] Decide whether the Redis counter represents all password attempts or failed attempts; align the API wording and `ShareService::CheckPasswordRateLimit` call placement with that decision.
- [ ] Preserve atomic counter/expiry behavior and add a concurrency test for the selected policy.
- [ ] Reconcile the conflicting public error contract for nonexistent shares versus password failures (`60001` versus the section 9.4.5 requirement to return `60003` without revealing existence).
- [ ] Add integration coverage for missing password, wrong password, nonexistent share, rate-limit rejection, and successful access after prior failures.

### P0.3 Add share-domain audit events

- [ ] Define one share audit boundary for `share_create`, `share_access`, `share_pwd_fail`, `share_download`, and `share_cancel`; do not duplicate SQL across controllers.
- [ ] Reconcile `operation_logs.user_id NOT NULL` with visitor events that have no authenticated owner, including the schema migration and retention/privacy rules.
- [ ] Record the fields required by API section 9.4.4 without logging passwords or raw Share Tokens.
- [ ] Add database-backed tests for successful events, rejected access, batch cancellation, and audit-write failure policy.

---

## P1 - Reproducible Validation

### P1.1 Make the full CTest run self-contained

The 2026-07-14 audit ran `ctest --preset linux-debug-clang --output-on-failure` from a clean `main` worktree. It reported 99%: 8 of 1155 enabled tests failed because their scripts reached `127.0.0.1:8080` while no server was running. Later integration scripts started a server and passed, so this is a test lifecycle/ordering defect rather than evidence for eight independent feature regressions.

- [ ] Give every backend integration test a consistent server lifecycle through a shared CTest fixture or `ensure_server` helper.
- [ ] Ensure one integration script cannot stop a server that later scripts assume is shared.
- [ ] Fix the affected integration entries: assembly backpressure, auth lifecycle, copy/delete atomicity, download flow, domain extraction invariants, file metadata queries, file mutation operations, and folder lifecycle.
- [ ] Run the full preset from an initially stopped server and require all enabled non-gated tests to pass without manual setup.
- [ ] Audit the 20 disabled tests and one migration-continuity skip; enable, replace, or remove obsolete cases and record the rationale for anything intentionally retained.
- [ ] Run the two S3 application/adapter tests with their gates enabled in a compatible environment; do not count their default skip result as current S3 execution evidence.

---

## P2 - Backend Documentation Truth Alignment

### P2.1 Repair active backend specification placeholders

- [ ] Replace the `TBD.` Purpose text in the backend-relevant OpenSpec capabilities: architecture decisions, deployment operations, documentation governance, persistence design, and validation/performance.
- [ ] Use an OpenSpec change to update documentation-governance wording that still calls the completed backend refactor roadmap active and points backend tasks at `docs/TODO.md` instead of the archive.
- [ ] Validate the updated backend-relevant OpenSpec capabilities with the repository's OpenSpec tooling.

### P2.2 Remove stale backend implementation commentary

- [ ] Update the obsolete TDD RED-stage header in `test/services/TokenService_test.cpp`, which still says Share Token support is unimplemented even though the tests are green.
- [ ] Re-scan `src/`, `test/`, `docs/design/`, and backend-relevant OpenSpec capabilities for stale `TODO`, `FIXME`, `TBD`, and “not implemented” claims after the work above is complete.

---

## Deferred Client Work

These items are intentionally paused, are not part of the active checklist, and do not block backend completion:

- Replace hard-coded Web E2E credentials with seeded fixtures or environment-provided test identities, and automate isolated Web test bootstrap.
- Manually verify Web folder-tree synchronization after folder create, rename, move, delete, and navigation; automate stable portions afterward.
- Manually verify Desktop visitor-download resume plus completed-file size/hash validation against a shared file; automate stable portions afterward.
- Replace the `TBD.` Purpose text in the Web client experience and Desktop client experience OpenSpec capabilities.
- Re-audit DOC-00 through DOC-06 against the split QML component structure, correct stale anchors/status labels, and deduplicate `[规划]` items.
- Prioritize genuinely accepted Desktop work such as owner-file pagination, view switching, drag/drop, loading skeletons, and associated Qt Quick coverage.

---

## Definition of Done

- [ ] The relevant authority documentation is updated before each behavior change.
- [ ] New or changed backend behavior has focused unit and integration coverage proportional to risk.
- [ ] `cmake --build --preset linux-debug-clang` succeeds.
- [ ] A full backend CTest run succeeds from an initially stopped server, excluding only explicitly documented environment gates.
- [ ] No completed historical checklist remains in this active file.
