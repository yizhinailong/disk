## 1. Source Inventory and Scope Control

- [x] 1.1 Inventory the remaining migration inputs in `docs/design/`, `docs/desktop/`, selected `docs/lunwen/`, `README.md`, and client README files.
- [x] 1.2 Classify each source document under one or more target capabilities: desktop-client-experience, documentation-governance, persistence-design, validation-and-performance, deployment-operations, or architecture-decisions.
- [x] 1.3 Mark any stale, aspirational, or implementation-conflicting source claims as follow-up reconciliation items instead of changing runtime behavior in this migration.

## 2. Desktop Client Experience Specs

- [x] 2.1 Convert the desktop shell model, owner/visitor/admin flow separation, authentication-domain isolation, and platform constraints into `openspec/specs/desktop-client-experience/spec.md`.
- [x] 2.2 Convert owner explorer information architecture, view-mode taxonomy, independent pages, layout rules, interaction matrices, and navigation/state models into the desktop capability spec.
- [x] 2.3 Convert implementation traceability from QML pages/components, C++ managers/models, context properties, and quick-test evidence into concise desktop requirements.
- [x] 2.4 Convert administrator desktop behavior and Chinese UI terminology governance into the desktop capability spec.

## 3. Documentation Governance Specs

- [x] 3.1 Define OpenSpec as the future requirement authority for migrated documentation areas.
- [x] 3.2 Define legacy document status for design, desktop, thesis, migration, README, and client README material as historical or migration-input context after this migration.
- [x] 3.3 Define behavior-preserving migration rules and follow-up handling for doc/code mismatches discovered during migration.

## 4. Persistence Design Specs

- [x] 4.1 Convert PostgreSQL and Redis persistence roles, schema responsibilities, naming conventions, primary indexes, and relationship contracts into `openspec/specs/persistence-design/spec.md`.
- [x] 4.2 Convert upload task lifecycle, uploaded chunk tracking, storage-reserved semantics, effective quota, and ref-count behavior into persistence requirements.
- [x] 4.3 Convert PostgreSQL migration rationale, rollback criteria, and database selection constraints from the ADR and database design docs into persistence requirements.

## 5. Validation and Performance Specs

- [x] 5.1 Convert backend unit, integration, system, security, and compatibility validation coverage into `openspec/specs/validation-and-performance/spec.md`.
- [x] 5.2 Convert desktop docs-only validation dimensions, evidence requirements, and failure severity rules into validation requirements.
- [x] 5.3 Convert pressure-test endpoints, `drogon_ctl press` usage, performance targets, result recording, and tool limitations into performance validation requirements.

## 6. Deployment Operations Specs

- [x] 6.1 Convert secure runtime configuration, environment variable requirements, service hardening, and secret-management constraints into `openspec/specs/deployment-operations/spec.md`.
- [x] 6.2 Convert build/deploy prerequisites, PostgreSQL and Redis setup, migration runbooks, service management, HTTPS/reverse proxy configuration, and operational troubleshooting into deployment requirements.
- [x] 6.3 Convert monitoring, logging, backup, restore, upgrade, and rollback procedures into operations requirements.

## 7. Architecture Decisions Specs

- [x] 7.1 Convert accepted PostgreSQL migration decision content into `openspec/specs/architecture-decisions/spec.md`.
- [x] 7.2 Convert Drogon streaming API evaluation and the decision to keep the current download streaming paths into architecture-decision requirements.
- [x] 7.3 Convert io_uring No-Go feasibility analysis and future reconsideration triggers into architecture-decision requirements.

## 8. Review and Verification

- [x] 8.1 Run `openspec status --change "migrate-remaining-docs-to-openspec" --json` and confirm `tasks` is done and the change is apply-ready.
- [x] 8.2 Review each generated capability spec to confirm every requirement has at least one `#### Scenario:` block and uses normative SHALL wording.
- [x] 8.3 Confirm proposal, design, specs, and tasks describe a documentation/specification migration only and do not require application code, API, database schema, or deployment behavior changes.
- [x] 8.4 Run `openspec status --change "migrate-remaining-docs-to-openspec"` and record the final human-readable status for the proposal summary.
