## Why

Five active backend OpenSpec capabilities still contain placeholder Purpose text, and documentation governance still describes the completed backend refactor roadmap as active work in `docs/TODO.md`. The backend documentation contract needs to reflect the repository's current separation between normative requirements, current open work, current decision status, and archived history.

## What Changes

- Replace the placeholder Purpose text in `architecture-decisions`, `deployment-operations`, `documentation-governance`, `persistence-design`, and `validation-and-performance` with concise statements of each capability's authoritative scope.
- Update documentation governance so the completed backend refactor roadmap points to `docs/archive/2026-07-14-backend-refactor-todo.md` as history.
- Reserve `docs/TODO.md` for verified, currently unfinished backend work and preserve traceability from that work to relevant OpenSpec requirements, design sources, and decision records.
- Keep `docs/backend-refactor-decisions.md` as the current decision and implementation-status record, `docs/backend-discovery.md` as a historical discovery baseline, and `docs/design/` as supporting reference material while OpenSpec remains the normative requirement authority.
- Do not change runtime behavior or the deferred client capability purposes.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `documentation-governance`: Correct the lifecycle and traceability contract for the completed backend refactor roadmap, the active backend backlog, current decision status, and historical discovery material.

## Impact

- OpenSpec artifacts: a documentation-governance delta and Purpose metadata updates in five active backend capability specs.
- Documentation references: `docs/TODO.md`, `docs/archive/2026-07-14-backend-refactor-todo.md`, `docs/backend-refactor-decisions.md`, `docs/backend-discovery.md`, and `docs/design/` retain explicit current, historical, decision, discovery, and supporting-reference roles.
- No application code, tests, database schema, public API, runtime configuration, dependencies, or client documentation/specification behavior changes.
- `desktop-client-experience` and `web-client-experience` remain outside this change.
