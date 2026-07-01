## Context

The backend refactor TODO splits early work into protection and discovery before low-risk cleanup and domain extraction. The discovery track must turn implicit behavior into an explicit safety contract for later refactors. Current behavior is distributed across Drogon global filters, route-level filters, controllers, services, cleanup jobs, database updates, and local filesystem operations.

The initial exploration surfaced several areas that need authoritative documentation and targeted verification: JWT may execute both globally and route-level for protected file routes; rate-limit filters all appear to fail open when Redis is unavailable; normal uploads reserve quota before creating upload tasks and transfer reserved bytes to used bytes during finalization; instant upload reuses content without increasing `storage_used`; copy increments `storage_used` even when reusing existing content; trash keeps logical content references until expiration; and private downloads currently do not update file download metadata while share downloads update `shares.download_count`.

This change intentionally treats discovery as a read-only/refactor-preparation capability. The implementation should produce documents and characterization checks, not change runtime behavior.

## Goals / Non-Goals

**Goals:**

- Produce a backend discovery note that maps current filter, rate-limit, upload, content, quota, trash, storage deletion, and download side-effect behavior.
- Confirm static findings with focused tests or runtime observations where behavior depends on framework execution order, especially global versus route-level filters.
- Identify behavior-preserving constraints that later refactor tasks must honor.
- Record unresolved product or architecture questions separately from confirmed current behavior.
- Keep evidence tied to source locations, tests, logs, or commands so future maintainers can validate the conclusions.

**Non-Goals:**

- Do not change public API response shapes.
- Do not remove duplicate filters, alter JWT enforcement, or change rate-limit policy in this change.
- Do not change quota accounting, content ref-counting, trash cleanup, download metadata updates, or storage deletion behavior in this change.
- Do not introduce new database schema, storage layout, or object storage abstractions in this change.
- Do not implement ContentService, QuotaService, UploadLifecycleService, TrashService, repositories, or transaction abstractions in this change.

## Decisions

### Decision: Treat discovery output as a behavior contract

The main artifact should be a backend discovery note that records confirmed current behavior and distinguishes it from recommended future changes. Later refactor PRs can cite this note as the baseline they must preserve unless a separate proposal explicitly changes behavior.

Alternative considered: immediately fix discovered issues such as duplicate JWT execution or quota inconsistencies. That would mix discovery with semantic changes and would undermine the TODO principle of preserving behavior before refactoring.

### Decision: Verify framework-dependent filter behavior dynamically

Static configuration is enough to identify likely duplicate JWT execution, but not enough to prove Drogon execution order or interactions between multiple `GlobalFilters` plugin entries and route-level filters. The implementation should add or run targeted characterization checks that observe filter counts/order for representative protected, public, upload, download, admin, and share paths.

Alternative considered: rely only on static config inspection. This is faster, but weaker, because the highest-risk question is specifically whether multiple filter declarations execute together at runtime.

### Decision: Separate confirmed behavior from open product decisions

The discovery note should have explicit sections for confirmed current behavior and unresolved decisions. Examples of unresolved decisions include whether `storage_used` means physical unique bytes or logical per-user bytes, whether private downloads should update file metadata, and whether any rate-limit path should fail closed on Redis errors.

Alternative considered: encode recommended future behavior directly in the discovery note. That would blur current-state documentation with product decisions and make later behavior-preserving refactors harder to review.

### Decision: Use targeted characterization over broad end-to-end coverage

The discovery implementation should prefer small tests, instrumentation, or reproducible manual checks that answer the TODO discovery questions directly. Full invariant test coverage belongs to the separate safety-net track.

Alternative considered: build the full Phase 0 invariant suite as part of this change. That would be valuable but too broad for a discovery proposal and would overlap with TODO Track 1.

## Risks / Trade-offs

- Static findings may be mistaken where framework behavior is non-obvious → Mitigate by adding targeted runtime/filter characterization before marking findings confirmed.
- Discovery artifacts may become stale as parallel refactor branches evolve → Mitigate by citing source locations and commands, and re-run checks before applying later behavior-changing proposals.
- Capturing known inconsistencies without fixing them may feel incomplete → Mitigate by clearly labeling them as future decisions and keeping this change behavior-preserving.
- Characterization checks can overfit current implementation details → Mitigate by testing observable behavior and side effects rather than private helper structure where possible.
- Existing accounting behavior may reveal product-rule contradictions → Mitigate by documenting both current implementation and the decision needed before changing semantics.
