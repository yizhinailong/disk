## Context

The backend refactor TODO places invariant tests in Phase 0 because later service-boundary work depends on knowing current upload, quota, content, trash, and path behavior. Existing Python integration tests already exercise the HTTP APIs through `uv run`, and CTest registers them serially from `test/CMakeLists.txt`. Those tests mainly verify response contracts and high-level flow success, while the refactor guardrails need to verify durable side effects in PostgreSQL and local storage.

The relevant current behavior to characterize includes upload task status transitions, reservation release, storage-used accounting, file/content row creation or reuse, content reference counts, trash lifecycle effects, and folder subtree path updates. The change should avoid production behavior changes and should make current semantics visible even where open TODO questions remain.

## Goals / Non-Goals

**Goals:**

- Add executable integration safety tests for the Safety Net items in `docs/TODO.md` A1 through A5.
- Provide reusable Python helpers for PostgreSQL state inspection and storage path checks.
- Keep tests deterministic by using isolated test data, before/after snapshots, and unique filenames/folders.
- Register the new tests in CTest using the existing `uv run` pattern and serial execution.
- Characterize current product rules, including any surprising behavior, without changing those rules.

**Non-Goals:**

- Refactor backend services or change upload/content/quota/trash behavior.
- Introduce DB fault-injection infrastructure for disabled C++ model tests.
- Replace existing API integration tests.
- Solve open product questions such as whether trash should count against quota; this change records the observed current behavior as test expectations.

## Decisions

1. **Use Python integration tests for DB/FS invariants.**
   - Decision: Add safety-net scripts under `test/integration/` and register them as CTest tests.
   - Rationale: The existing integration-test contract already runs Python scripts via `uv run`, can drive the real HTTP API, and can inspect actual database/storage state after each operation.
   - Alternative considered: Add C++ unit tests only. This would be faster for DTO/storage model checks but would not prove full HTTP-to-DB-to-filesystem behavior.

2. **Add a small PostgreSQL helper instead of embedding SQL boilerplate in every test.**
   - Decision: Add `test/integration/lib_py/db.py` with query helpers that load connection settings from environment variables first and `config.json` second.
   - Rationale: Invariant tests need repeated snapshots from `users`, `upload_tasks`, `files`, `file_contents`, `trash`, and `upload_task_chunks`.
   - Alternative considered: Use shelling out to `psql`. That would avoid a Python dependency but would make parameterized queries, portability, and error reporting weaker.

3. **Use isolated data and delta assertions.**
   - Decision: Tests should create unique users or unique file/folder names and compare before/after state instead of assuming a pristine database.
   - Rationale: Existing integration tests often use the default admin account; invariant tests should not fail because of unrelated pre-existing files or quotas.
   - Alternative considered: Reset the whole database before each test. That would be stronger isolation but too destructive and unsuitable for shared developer environments.

4. **Split safety tests by invariant domain.**
   - Decision: Use separate scripts for upload invariants, content/quota invariants, and move/copy/path invariants.
   - Rationale: The TODO sections A1-A5 are independent enough to run and debug separately, while keeping each test file focused.
   - Alternative considered: One large safety-net script. That would simplify CMake registration but make failures harder to localize and slower to iterate.

5. **Characterize current behavior, not desired future behavior.**
   - Decision: Where current rules are debatable, such as instant-upload storage accounting or trash quota treatment, tests should assert the current observed behavior and name it clearly.
   - Rationale: Phase 0 protects refactors from accidental changes. Intentional product-rule changes can later modify the tests with a separate proposal.
   - Alternative considered: Change tests to desired future semantics now. That would mix safety-net work with product behavior changes.

## Risks / Trade-offs

- **Risk: Tests become flaky because old data already exists.** → Mitigate with unique names, content generated inside each script, query filters by created entities, and delta assertions against captured snapshots.
- **Risk: Direct DB assertions couple tests to schema details.** → Mitigate by limiting assertions to tables and columns explicitly named in the refactor TODO as invariants.
- **Risk: Adding a PostgreSQL Python dependency slows first `uv run`.** → Mitigate by declaring dependencies only on the new DB-aware scripts and keeping helpers small.
- **Risk: Storage path assertions depend on local filesystem layout.** → Mitigate by resolving paths from `config.json` and only asserting the existing local-storage behavior documented by the TODO.
- **Risk: Some expiry or cleanup behavior is difficult to trigger through public APIs.** → Mitigate by preparing minimal DB fixtures for expired upload tasks/trash rows and invoking existing public/admin/cleanup paths where available; if no stable trigger exists, keep that scenario explicit in tasks as requiring implementation-time discovery.

## Migration Plan

1. Add integration helpers for DB access and storage path resolution.
2. Add safety-net scripts incrementally, starting with upload success/cancel invariants.
3. Add CTest registrations with `RUN_SERIAL TRUE` to avoid state races with existing integration tests.
4. Run targeted new CTest entries and then the broader integration suite where environment services are available.
5. Rollback is safe by removing the new test files and CTest registrations; no production state or schema changes are introduced.

## Open Questions

- Which cleanup trigger should the expired-upload invariant use if no stable public endpoint exists?
- Should the implementation create a dedicated test user per script, or use unique entities under the configured test account with before/after snapshots?
- Should CTest timeouts remain at 120 seconds for all new scripts, or should upload/ref-count scenarios use a longer timeout similar to the upload rate-limit test?
