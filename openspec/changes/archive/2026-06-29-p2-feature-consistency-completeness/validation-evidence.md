# Validation Evidence

Date: 2026-06-29
Change: p2-feature-consistency-completeness

## Focused commands run

### Backend/API integration test syntax

Command:

```bash
python3 -m py_compile test/integration/test_admin_flow.py test/integration/test_download_flow.py
```

Result: passed with no output.

Coverage evidence recorded in the scripts:

- `test/integration/test_admin_flow.py` includes admin available-space update success, invalid payload rejection, non-admin/unauthenticated rejection, and admin operation-log visibility for `admin.user.available_space_change`.
- `test/integration/test_download_flow.py` includes owner and visitor metadata integrity assertions, owner and visitor `206` ranged download assertions, and owner and visitor `416` invalid range assertions.

### Web focused unit tests

Command:

```bash
npm --prefix clients/disk-web test -- --run src/stores/__tests__/admin.test.ts src/stores/__tests__/drive.test.ts src/stores/__tests__/transfer.test.ts src/composables/__tests__/useDownload.test.ts
```

Result: blocked by missing local dependencies.

Observed output:

```text
sh: line 1: vitest: command not found
```

Additional check:

```bash
test -d clients/disk-web/node_modules && printf present || printf missing
```

Result: `missing`.

Known blocker: install Web dependencies before running Vitest in this worktree.

### Desktop focused unit tests

Commands:

```bash
cmake --build clients/desktop/build --target test_transfer_manager test_download_task_model
cmake --build build --target test_transfer_manager test_download_task_model
```

Results: blocked by missing configured CMake build directory.

Observed output:

```text
Error: /home/liufeng/workspace/disk/.claude/worktrees/rustling-growing-parnas/clients/desktop/build is not a directory
Error: not a CMake build directory (missing CMakeCache.txt)
```

Additional checks:

```bash
find clients/desktop -maxdepth 3 -type d -name build -print
find . -maxdepth 3 -type d -name build -print
```

Results: no `clients/desktop/build`; `./build` exists but is not configured with CMake.

Known blocker: configure the Desktop CMake build directory before running focused Qt tests.
