## Why

The project still keeps several stable documentation authorities outside OpenSpec, especially the desktop-client documentation series and supporting backend design documents for schema, validation, deployment, and architecture decisions. Migrating these remaining contracts into OpenSpec will make future changes traceable through capability specs instead of relying on large standalone Markdown documents.

## What Changes

- Add OpenSpec capabilities for the remaining desktop product documentation, including navigation, layout, interaction, state, implementation mapping, admin UI, and terminology rules.
- Add OpenSpec capabilities for documentation governance, validation plans, persistence/schema design, deployment runbooks, and architecture decision records.
- Preserve existing runtime behavior: this change is a documentation/specification migration only.
- Establish implementation tasks that convert the remaining `docs/design/`, `docs/desktop/`, and selected `docs/lunwen/` source material into capability-oriented OpenSpec specs.

## Capabilities

### New Capabilities
- `desktop-client-experience`: Desktop client information architecture, navigation model, layout and interaction behavior, view states, QML/component mapping, admin UI behavior, and Chinese UI terminology.
- `documentation-governance`: Rules for choosing OpenSpec as the authoritative requirement source and governing legacy design, desktop, thesis, and migration documents.
- `persistence-design`: Database schema, entity relationships, indexes, PostgreSQL migration constraints, and persistence design contracts not already captured by runtime configuration.
- `validation-and-performance`: System, unit, desktop, integration, migration, and pressure/performance validation requirements derived from existing test-plan documents.
- `deployment-operations`: Deployment, operations, secure runtime setup, backup/restore, monitoring, and maintenance runbook requirements.
- `architecture-decisions`: Architecture decision records and exploratory technical analysis requirements, including PostgreSQL migration rationale, async streaming evaluation, and io_uring feasibility analysis.

### Modified Capabilities

None. Existing backend/domain capabilities remain the baseline; this change adds missing documentation coverage without changing their requirements.

## Impact

- Adds OpenSpec artifacts under `openspec/changes/migrate-remaining-docs-to-openspec/` and, when applied/synced, new capability specs under `openspec/specs/`.
- Affects documentation sources in `docs/design/`, `docs/desktop/`, `docs/lunwen/`, `README.md`, and client README files as migration inputs.
- Does not change application code, API routes, database schema, build configuration, or deployed behavior.
- Future work should update OpenSpec capability specs first and treat legacy documents as historical or derived references after this migration.
