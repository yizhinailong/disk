## Why

The backend refactor roadmap still contains open product and policy questions that block later behavior-changing work around quota accounting, download metadata side effects, JWT filter cleanup, and Redis rate-limit failure handling. This change records the decisions as documentation-only planning artifacts so future implementation changes can proceed from an explicit contract without mixing business code edits into the decision step.

## What Changes

- Add a backend refactor decisions note that records selected semantics for storage accounting, instant upload, trash quota, download metadata side effects, JWT enforcement, and Redis rate-limit failures.
- Update the active backend TODO roadmap to mark the covered decision/documentation items as closed or linked to the new decision note.
- Establish logical per-user bytes as the target meaning of `storage_used`.
- Specify that instant upload should increase `storage_used` consistently with copy semantics.
- Specify that trashed items continue to count against quota until permanent deletion or expiry cleanup.
- Specify that private downloads should update file-level download metadata.
- Specify that share downloads should update share-level metadata and file-level metadata.
- Specify `global-with-exemptions` as the chosen JWT enforcement strategy for future duplicate-filter cleanup.
- Specify that rate-limit Redis failures remain fail-open for now, with explicit documentation and tests in later implementation work.
- Do not change business code in this proposal.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `documentation-governance`: Record how the active backend TODO and backend decision note distinguish current implementation, accepted product decisions, and future implementation tasks.
- `architecture-decisions`: Preserve accepted backend refactor policy decisions as durable decision records that remain behavior-preserving until implemented by separate changes.
- `file-transfer`: Update target requirements for instant-upload accounting and download metadata side effects.
- `trash-lifecycle`: Confirm the quota rule that trash remains storage-consuming until permanent deletion or expiry cleanup.
- `request-filter-application`: Update target requirements for JWT ownership and Redis rate-limit failure policy.

## Impact

- Documentation artifacts: `docs/backend-refactor-decisions.md`, `docs/TODO.md`.
- OpenSpec artifacts: proposal, design, delta specs, and tasks for this documentation-only change.
- No application code, database schema, runtime configuration, dependencies, or public API behavior changes are intended in this change. Later implementation changes will be required for any runtime behavior that currently differs from the accepted decisions.
