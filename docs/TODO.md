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
- The operation-specific share rate-limit implementation and its ten-item evidence
  matrix are archived at
  [`docs/archive/2026-07-19-share-operation-rate-limits.md`](archive/2026-07-19-share-operation-rate-limits.md).
- No active backend implementation item remains. New backend behavior work must be
  proposed through the governing OpenSpec capability before it is added here.
- `openspec validate --all --strict --no-interactive` currently validates 22 of 24
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
