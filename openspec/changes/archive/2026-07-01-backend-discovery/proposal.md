## Why

The backend refactor TODO identifies Discovery / 架构发现 as a prerequisite for safe domain extraction, but the current upload, content, quota, trash, filter, and download-side-effect rules are implicit and spread across services, controllers, filters, and cleanup jobs. Capturing and validating these rules now reduces the risk of changing behavior accidentally during later ContentService, QuotaService, UploadLifecycleService, TrashService, and filter cleanup work.

## What Changes

- Add a backend discovery capability that produces authoritative maps of current consistency-sensitive backend behavior before implementation refactors begin.
- Document current filter execution expectations, including global filters, route-level filters, JWT duplication risk, path-based rate limits, public exemptions, and Redis fail-open behavior.
- Document current upload, content ref-count, quota/accounting, trash cleanup, physical storage deletion, and download metadata side effects.
- Add characterization/verification tasks that confirm static findings with targeted tests or runtime observations before using them as refactor constraints.
- No public API response shape changes are proposed by this discovery change.
- No storage layout, database schema, or behavior-changing refactor is proposed by this change.

## Capabilities

### New Capabilities
- `backend-discovery`: Captures and verifies current backend consistency, filter, accounting, lifecycle, and side-effect behavior as a refactor safety contract.

### Modified Capabilities

None. This change documents and validates current behavior; it does not intentionally change existing product requirements.

## Impact

- Affected documentation and planning areas: `docs/TODO.md`, backend discovery notes, and OpenSpec artifacts for the new discovery capability.
- Affected backend areas for investigation and characterization: `src/filters/*`, `config.json`, `src/controllers/FileController.*`, `src/controllers/ShareController.*`, `src/services/UploadService.*`, `src/services/FileMutationService.*`, `src/services/FileQueryService.*`, `src/services/ShareService.*`, `src/services/CleanupService.*`, and storage abstractions under `src/storage/*`.
- Affected risk domains: JWT/filter execution order, Redis rate-limit failure policy, upload task state transitions, reserved/used storage accounting, content ref-count lifecycle, trash retention semantics, physical blob deletion safety, and download metadata updates.
- No new runtime dependency is expected.
