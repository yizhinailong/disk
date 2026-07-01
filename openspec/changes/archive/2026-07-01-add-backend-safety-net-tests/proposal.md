## Why

The backend refactor TODO identifies Safety Net / invariant tests as the first protection layer before semantic refactors. Current integration tests cover many API responses, but they do not consistently assert the database and filesystem side effects that future service-boundary refactors could accidentally break.

## What Changes

- Add a backend safety-net integration test capability that characterizes current upload, content deduplication, quota, trash, move/copy, and path invariants.
- Add shared integration-test helpers for reading backend database state and storage artifacts in a deterministic way.
- Register the new safety tests in CTest so they run through the existing `uv run` Python integration-test contract.
- Preserve public API behavior; this change adds guardrails only and does not intentionally change product semantics.

## Capabilities

### New Capabilities
- `backend-safety-net`: Defines executable backend invariant tests for upload lifecycle, quota accounting, content reference counts, trash cleanup, and file/folder path consistency.

### Modified Capabilities

None. This change adds test coverage for existing behavior without changing product requirements.

## Impact

- Affected test code: `test/integration/`, `test/integration/lib_py/`, and `test/CMakeLists.txt`.
- Affected runtime dependencies for tests: Python integration scripts may add a PostgreSQL client dependency through PEP 723 metadata.
- Affected backend areas under test: upload APIs, file mutation APIs, folder APIs, trash APIs, database tables (`users`, `upload_tasks`, `upload_task_chunks`, `files`, `file_contents`, `trash`), and local storage paths configured by `config.json`.
- No breaking API changes and no intended production-code behavior changes.
