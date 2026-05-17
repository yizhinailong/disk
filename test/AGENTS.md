# BACKEND TEST KNOWLEDGE

## OVERVIEW

`test/` covers the backend with GoogleTest unit suites, Python `uv run` integration scenarios, and shell benchmark scripts.

## STRUCTURE

```text
test/
├── CMakeLists.txt       # disk-test + Python integration CTest registrations
├── main.cpp             # GoogleTest entry; initializes libsodium once
├── dtos/                # DTO validation/serialization tests
├── filters/             # JwtAuth/ShareAuth/AdminAuth filter tests
├── services/            # service/domain unit tests
├── storage/             # AssemblyWorkerPool tests
├── utils/               # Config/hash/response/key tests
├── mocks/               # MockDbClient, MockRedisClient
├── integration/         # Python end-to-end tests + lib_py harness
└── benchmark/           # HTTP load scripts using curl/drogon_ctl
```

## WHERE TO LOOK

| Need | Location | Notes |
|------|----------|-------|
| Unit-test DTO validation | `dtos/*_test.cpp` | Match request rules from `src/dtos/*.hpp` |
| Unit-test service behavior | `services/*_test.cpp` | Some suites are characterization baselines |
| Unit-test filters | `filters/*_test.cpp` | Auth token parsing/filter continuation behavior |
| Integration helpers | `integration/lib_py/` | Shared `fetch`, `ensure_server`, assertions, fixtures, evidence |
| Add integration scenario | `integration/test_*.py`, `CMakeLists.txt` | Register with `uv run`, `RUN_SERIAL TRUE`, repo root workdir |
| Benchmark endpoint | `benchmark/bench_*.sh` | Respect upload rate limit defaults |

## TEST TYPES

| Layer | Framework | Command |
|-------|-----------|---------|
| Backend C++ unit | GoogleTest + CTest | `ctest --preset linux-debug-clang -R FileService -V` |
| Direct GTest | `disk-test` | `./build/linux-debug-clang/test/disk-test --gtest_filter=Suite.*` |
| Backend integration | Python scripts via `uv run` | `uv run test/integration/test_upload_flow.py` |
| Harness smoke | Python `uv run` | `uv run test/integration/harness_smoke.py` |
| Benchmarks | shell + curl/drogon_ctl | `BENCH_N=1000 bash test/benchmark/run_all.sh` |

## CONVENTIONS

- C++ unit files are usually `{Component}_{Aspect}_test.cpp`; suites use GoogleTest `TEST` / fixture macros.
- Python integration files are `test_<scenario>.py` and should include PEP 723 metadata with `requires-python = ">=3.10"` and dependencies.
- Integration tests are standalone scripts but share `lib_py`: `common`, `http`, `auth`, `assertion`, `fixtures`.
- `ensure_server()` may auto-start `./build/linux-debug-clang/src/disk` and inject dev `JWT_SECRET`; tests still assume MySQL/Redis are available.
- Integration tests are serial in CTest; do not make them parallel without isolating DB/Redis/server state.
- Evidence belongs in `.sisyphus/evidence/` through helper functions, not ad hoc files in source dirs.
- `FileServiceAtomicity_test.cpp` contains enabled characterization tests and disabled fault-injection tests; do not delete disabled tests just to pass.

## PYTHON HARNESS CONTRACT

| Module | Purpose |
|--------|---------|
| `common.py` | logging, pass/fail counters, evidence save, summary exit |
| `http.py` | `fetch`, JSON field lookup, headers, Redis cleanup helpers |
| `auth.py` | server readiness, auto-start, cleanup, login helpers |
| `assertion.py` | status/JSON/header assertions |
| `fixtures.py` | temp files, MD5 hashes, unique names |

## ANTI-PATTERNS

- Do not run integration tests in parallel; shared state causes flakes.
- Do not bypass `lib_py` helpers for repeated HTTP/assertion/evidence patterns.
- Do not add integration tests without CTest registration when they are part of the standard suite.
- Do not exceed upload benchmark defaults casually; `bench_upload_init.sh` warns about 240 requests/min/user.
- Do not remove failing characterization tests; fix product behavior or update the baseline deliberately.

## COMMANDS

```bash
ctest --preset linux-debug-clang -V
ctest --preset linux-debug-clang -R UploadFlowIntegration -V
./build/linux-debug-clang/test/disk-test --gtest_list_tests
./build/linux-debug-clang/test/disk-test --gtest_filter=FileServiceAtomicityTest.*
uv run test/integration/test_auth_flow.py
uv run test/integration/harness_smoke.py
bash test/benchmark/run_all.sh
```
