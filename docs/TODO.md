# Backend TODO

> Updated: 2026-07-18
>
> This file tracks verified, currently open backend work only. Client implementation,
> client validation, and client-documentation synchronization are paused and listed
> without checkboxes under Deferred Client Work. Completed work must be removed from
> the active checklist or moved to `docs/archive/`; it must not remain here as a checked
> historical log.
>
> The completed backend refactor roadmap is archived at
> [`docs/archive/2026-07-14-backend-refactor-todo.md`](archive/2026-07-14-backend-refactor-todo.md).
> The self-contained backend CTest closure is archived at
> [`docs/archive/2026-07-18-ctest-self-contained.md`](archive/2026-07-18-ctest-self-contained.md).
> The backend implementation-marker audit is archived at
> [`docs/archive/2026-07-18-backend-implementation-marker-audit.md`](archive/2026-07-18-backend-implementation-marker-audit.md).

## Working Rules

- Update the relevant design, API, product, or test authority document before changing behavior.
- Mark an item complete only when implementation and proportionate tests both exist.
- Treat an environment-gated test as verified only when it actually runs; a skip is not a pass for the gated behavior.
- Do not update `clients/`, `docs/desktop/`, or client-only OpenSpec requirements during this backend phase unless a backend contract change requires a compatibility note.

---

## P0 - Share Security Contract Closure

The Share Token scope and live-state contract is reconciled in API section 9.4.2, with
the evidence audit recorded in section 9.4.5. The remaining open checklist gap is
operation-specific rate limits.

### P0.4 Separate sensitive share-operation rate limits

- [ ] Replace the shared `rate:share_public:{ip}` bucket with independent access, browse, and download limits matching API section 9.4.3.
- [ ] Keep access keyed by client IP; key browse and download by a verified token identifier or hash without storing or logging the raw Share Token.
- [ ] Define configuration and executable tests for each bucket before checking the API section 9.4.5 item complete.

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
