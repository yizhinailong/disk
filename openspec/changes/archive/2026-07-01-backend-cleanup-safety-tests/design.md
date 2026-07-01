## Context

The backend already has scheduled cleanup behavior for expired upload tasks and expired trash items. Existing OpenSpec requirements also identify the safety invariants that must be protected: expired upload cleanup, permanent trash cleanup, content reference accounting, quota accounting, and upload completion deduplication. This change is a safety-net worktree for later lifecycle refactors, so implementation should stay narrow and avoid moving business logic unless a small test seam is required.

Current cleanup behavior is centralized through `CleanupService`, which delegates expired upload handling to upload lifecycle code and expired trash handling to trash lifecycle code. The tests added here should exercise those same paths deterministically instead of waiting for production scheduler timing.

## Goals / Non-Goals

**Goals:**

- Provide a deterministic cleanup trigger usable by integration tests or manual maintenance paths.
- Keep production scheduled cleanup behavior unchanged and ensure it continues to use the same cleanup implementation.
- Cover expired in-progress upload cleanup invariants: terminal state, reservation release, no logical file creation, and temporary artifact cleanup.
- Cover expired trash cleanup invariants: permanent-delete semantics, content reference accounting, used-storage release, and blob deletion only after zero-reference verification.
- Cover the upload completion deduplication race where matching content appears between upload initialization and completion.

**Non-Goals:**

- Do not perform broad upload, trash, quota, or content lifecycle refactors in this worktree.
- Do not introduce a public user-facing cleanup API.
- Do not change product semantics for storage accounting, content reference counts, trash retention, or upload deduplication beyond preserving and testing current behavior.
- Do not replace the scheduler or change cleanup batch limits except where needed for deterministic tests.

## Decisions

1. Keep one cleanup implementation and add only a deterministic seam around it.
   - Decision: tests/manual triggers should call the same cleanup code that scheduled cleanup uses. If existing `CleanupService` methods are already sufficient, stabilize and reuse them rather than creating a parallel code path.
   - Rationale: safety tests are only valuable if they exercise production cleanup semantics.
   - Alternative considered: create test-only cleanup logic that directly mutates tables. Rejected because it can pass while production cleanup remains broken.

2. Treat the seam as internal/test/manual, not as a public API.
   - Decision: expose the seam through service/helper code or test harness wiring, not a user-facing endpoint.
   - Rationale: deterministic execution is needed for tests and maintenance, but externally callable cleanup would broaden the operational surface.
   - Alternative considered: add an HTTP admin endpoint. Rejected unless an existing admin-operation pattern explicitly requires it; this proposal does not require a public API change.

3. Write integration-style safety tests around observable invariants.
   - Decision: tests should prepare expired upload/trash state, invoke the deterministic cleanup path, and assert database plus filesystem/blob outcomes.
   - Rationale: lifecycle regressions often occur across service, database, quota, and storage boundaries, so unit-only assertions would miss the intended protection.
   - Alternative considered: mock storage/database interactions. Rejected for these scenarios because reference-count and artifact cleanup correctness depends on real persistence boundaries.

4. Characterize current dedup/accounting behavior instead of redefining it.
   - Decision: the upload completion dedup race test should assert consistency with the current backend rule and make the observed rule clear in the test name or assertion message.
   - Rationale: this worktree lays protection for later refactors and should not silently change product behavior.
   - Alternative considered: normalize dedup/reference-count behavior as part of this change. Rejected as a larger lifecycle refactor outside scope.

## Risks / Trade-offs

- Cleanup tests may be flaky if they depend on wall-clock expiry comparisons → Prepare explicitly expired timestamps and invoke cleanup synchronously through the deterministic seam.
- Tests may become too coupled to implementation details → Assert durable invariants in database/storage state while reusing existing public/service lifecycle entry points.
- Existing cleanup methods may already be deterministic enough → Prefer reusing them; only add wrapper/helper code when needed for stable test setup or execution.
- Shared database/storage state may cause cross-test interference → Run safety tests through the existing serial integration-test contract and isolate fixture names/users/content hashes.
- Blob deletion races are hard to simulate fully → Cover the zero-reference and still-referenced cases needed to prove cleanup does not delete shared content prematurely.
