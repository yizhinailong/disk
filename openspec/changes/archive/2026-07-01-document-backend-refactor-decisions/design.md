## Context

`docs/TODO.md` identifies several open backend refactor decisions in Phase 0 and Phase 2. `docs/backend-discovery.md` documents the current implementation state and shows that several runtime behaviors are intentionally pending product or policy decisions:

- `storage_used` is currently inconsistent across deduplicated instant upload and copy semantics.
- Trashed items currently keep counting against `storage_used` until permanent deletion or expiry cleanup.
- Private downloads currently do not update file-level metadata.
- Share downloads currently update share-level download count only, not file-level metadata.
- Protected routes can execute JWT through both global and route-level filter paths.
- Rate-limit Redis failures currently fail open.

This change is documentation-only. It records target decisions and updates the roadmap so later implementation changes can be scoped and tested separately.

## Goals / Non-Goals

**Goals:**

- Create a durable backend refactor decision note at `docs/backend-refactor-decisions.md`.
- Update `docs/TODO.md` so completed decision work links to the new note and leaves implementation follow-ups explicit.
- Treat `storage_used` as logical per-user bytes for future accounting behavior.
- Align instant upload accounting with copy semantics by requiring reused content to consume per-user logical quota.
- Preserve the trash quota rule that recoverable trash consumes quota until permanent deletion or expiry cleanup.
- Choose file-level metadata updates for private downloads.
- Choose both share-level and file-level metadata updates for share downloads.
- Choose global JWT enforcement with explicit public exemptions as the target duplicate-filter cleanup strategy.
- Keep Redis rate-limit failures fail-open for now, with explicit test coverage in the later implementation change.

**Non-Goals:**

- No business code changes.
- No database schema changes.
- No route, response-envelope, or public API behavior changes in this proposal step.
- No implementation of JWT filter cleanup or download/accounting side effects yet.
- No decision on object storage compatibility timing beyond leaving the existing TODO open.

## Decisions

### Decision: `storage_used` means logical per-user bytes

`users.storage_used` will represent the logical bytes charged to a user, not globally unique physical bytes stored in `file_contents`.

Rationale: Copy already charges the user for copied logical file bytes even when content is reused. Treating quota as logical per-user usage is easier for users to understand and avoids exposing deduplication internals as quota behavior.

Alternatives considered:

- Physical unique bytes: rejected because it conflicts with existing copy behavior and makes quota depend on backend dedup state rather than user-visible namespace state.
- Hybrid accounting: rejected for now because it would require explaining different rules for upload, instant upload, copy, and trash.

### Decision: instant upload increases `storage_used`

Instant upload will create another logical file reference and will therefore increase `storage_used` by the uploaded file size, subject to quota checks.

Rationale: This aligns instant upload with copy semantics and with logical per-user bytes. It also prevents users from bypassing logical quota by uploading content that already exists physically.

Alternatives considered:

- Keep current no-increase behavior: rejected because it is inconsistent with copy and makes quota behavior depend on whether content already exists.

### Decision: trash counts against quota until permanent deletion or expiry cleanup

Soft-deleted items remain recoverable user content and continue consuming quota. Storage is released only when trash state is permanently removed manually or by expiry cleanup.

Rationale: This matches current implementation and existing `trash-lifecycle` requirements. It also keeps restore behavior intuitive: recoverable content still belongs to the user.

Alternatives considered:

- Release quota on soft delete: rejected because it would allow quota relief while content remains recoverable and would require re-reservation or restore failure handling.

### Decision: private downloads update file-level metadata

Successful private file content downloads will update `files.download_count` and `files.last_accessed_at`.

Rationale: File-level metadata should reflect owner/private access activity. The current no-update behavior is documented as current implementation, but the target behavior is more useful for audit and UI metadata.

Alternatives considered:

- Preserve current no-update behavior: rejected because the metadata fields become misleading if downloads never update them.
- Update on download-info requests: rejected because metadata should reflect content transfer attempts, not metadata lookups.

### Decision: share downloads update share-level and file-level metadata

Successful share content downloads will update share-level download count and file-level download metadata.

Rationale: Share-level counters answer “how much was this share used?” while file-level metadata answers “how much was this file accessed?”. Keeping both makes the two views consistent without losing share analytics.

Alternatives considered:

- Share-level only: rejected because file-level metadata would undercount real content downloads.
- File-level only: rejected because it would remove share-specific analytics already present today.

### Decision: JWT enforcement uses global-with-exemptions

JWT enforcement will be owned by global filter configuration with explicit exemptions for public auth, health, and public share routes. Protected routes should not also declare route-level JWT when global JWT already covers them.

Rationale: Global JWT with exemptions provides a single default-secure authentication boundary and reduces per-route drift. It also directly resolves the duplicate JWT execution risk described in discovery.

Alternatives considered:

- Route-level-only JWT: rejected because every protected route must be audited and kept in sync individually.
- Keep both global and route-level JWT: rejected because it causes duplicate filter execution and can duplicate token validation work.

### Decision: Redis rate-limit failures remain fail-open for now

All existing rate-limit families remain fail-open if Redis increment/check operations fail. The later implementation change must make this explicit in code and tests.

Rationale: The current service is file-storage oriented, and accidental Redis outages should not make core authenticated or public share flows unavailable by default. This preserves current behavior while making the risk visible.

Alternatives considered:

- Fail-closed for all rate limits: rejected because Redis availability would become a hard dependency for broad API availability.
- Fail-closed only for selected sensitive endpoints: deferred until there is a separate risk-based requirement for those endpoints.

## Risks / Trade-offs

- [Risk] Documentation records target behavior that differs from current implementation for instant upload and download metadata. → Mitigation: `docs/backend-refactor-decisions.md` must label current behavior versus accepted target behavior, and `docs/TODO.md` must leave implementation tasks open.
- [Risk] Global JWT exemptions can accidentally expose protected routes if exemption patterns are too broad. → Mitigation: later implementation must keep public auth, health, and public share exemptions explicit and add representative tests proving protected routes execute JWT exactly once.
- [Risk] Fail-open rate limiting allows traffic bursts during Redis outages. → Mitigation: document fail-open as a deliberate temporary policy and require tests/logging to make the behavior visible.
- [Risk] Updating file metadata on shared downloads may create additional write load. → Mitigation: later implementation can batch, debounce, or accept the write cost after measuring; this proposal only records the policy.

## Migration Plan

1. In this documentation-only change, add `docs/backend-refactor-decisions.md` with decision records for the selected policies.
2. Update `docs/TODO.md` Phase 0 and Phase 2 items so the decision/documentation checkboxes are closed and implementation work remains open.
3. In later OpenSpec changes, implement runtime behavior separately:
   - quota/accounting behavior for instant upload;
   - file metadata updates for private and share downloads;
   - JWT duplicate-filter cleanup using global-with-exemptions;
   - explicit fail-open Redis rate-limit tests and documentation in code.
4. Rollback for this change is documentation-only: revert the docs and OpenSpec artifacts if the decisions are rejected before implementation.

## Open Questions

- Should object storage compatibility be a near-term requirement or only a design constraint? This remains outside the selected decision set.
- Should copy accounting move to a reservation-style model? This remains a later accounting consistency decision.
- Should any endpoint eventually become fail-closed for Redis rate-limit failures? This remains deferred until there is a stronger security or abuse requirement.
