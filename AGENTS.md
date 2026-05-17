# PROJECT KNOWLEDGE BASE

**Generated:** 2026-05-17  
**Commit:** 2e3c08b  
**Branch:** main

## OVERVIEW

Disk is a C++23 network-disk system: a Drogon REST backend plus a separate Qt/QML desktop client. Backend data lives in MySQL + Redis + content-addressed local storage; tests combine GoogleTest, Python `uv run` integration scripts, Qt Test, and Qt Quick Test.

## STRUCTURE

```text
disk/
├── src/                 # Drogon backend executable `disk`
├── test/                # Backend GoogleTest + Python integration + benchmark scripts
├── clients/desktop/     # Independent Qt6/QML desktop app `disk-desktop`
├── docs/design/         # Chinese backend architecture/API/DB/deploy/test docs
├── docs/desktop/        # Chinese desktop client authority docs DOC-00..DOC-07
├── sql/init.sql         # MySQL schema; includes compatibility-only deprecated fields
├── scripts/             # watchexec rebuild/run loops
├── config.json          # Drogon runtime config; dev DB/Redis defaults
├── CMakePresets.json    # Linux/Windows Clang presets with vcpkg toolchain
└── vcpkg.json           # Drogon/jwt-cpp/libsodium/gtest manifest
```

Ignore generated/artifact paths: `build/`, `clients/desktop/build/`, `.sisyphus/evidence/`, `.sisyphus/notepads/`, `output/`, `__pycache__/`.

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add backend API | `src/controllers/`, `src/dtos/`, `src/services/` | Controller parses; DTO validates; service owns business flow |
| Change file upload/download | `src/services/FileService.cpp`, `src/storage/`, `src/dtos/FileDto.hpp` | Main hotspot: chunking, quota, dedup, copy/move/delete, Range download |
| Change auth/token behavior | `src/services/AuthService.*`, `TokenService.*`, `src/filters/` | Access/refresh/share token flows; Redis revocation/cache state |
| Change sharing/trash/admin | `src/services/{Share,Trash,Admin}Service.*`, matching DTO/controller | Keep domain errors in `ErrorCode.hpp` ranges |
| Change config/security | `config.json`, `src/utils/ConfigMgr.*`, `docs/design/05-部署运维指南.md` | Production secrets must be env-injected |
| Add backend unit test | `test/{services,dtos,filters,storage,utils}/` | C++ GoogleTest, often mirrors source domain |
| Add backend integration test | `test/integration/test_*.py`, `test/integration/lib_py/` | `uv run`; serial; may auto-start server |
| Change desktop app | `clients/desktop/src/`, `clients/desktop/qml/` | Independent Qt6 project; root CMake does not include it |
| Change desktop tests | `clients/desktop/tests/unit/`, `clients/desktop/tests/quick/` | Qt Test for C++; Qt Quick Test for QML |
| API reference | `docs/design/02-API接口设计.md` | Canonical route/request/response/error contract |
| Desktop product rules | `docs/desktop/00-桌面客户端系统概述与文档治理.md` | DOC-00..DOC-07 are the desktop authority source |

## CODE MAP

| Symbol | Type | Location | Role |
|--------|------|----------|------|
| `main` | function | `src/main.cpp` | Initializes libsodium, config, TokenService, StorageMgr, scheduled tasks, then `drogon::app().run()` |
| `FileService` | class | `src/services/FileService.hpp/.cpp` | Core file workflow: upload init/chunks/complete, download, rename/move/copy/delete/search |
| `AuthService` | class | `src/services/AuthService.hpp/.cpp` | Register/login/refresh/logout backend business logic |
| `TokenService` | singleton | `src/services/TokenService.hpp/.cpp` | Access/refresh/share JWT lifecycle; 5s revocation negative cache |
| `RedisService` | singleton | `src/services/RedisService.hpp/.cpp` | Redis wrapper, batch ops, CAS, rate-limit increments |
| `IFileStorage` | interface | `src/storage/IFileStorage.hpp` | Filesystem boundary; no HTTP/DB/permission logic |
| `AssemblyWorkerPool` | singleton | `src/storage/AssemblyWorkerPool.hpp` | Upload assembly concurrency + same-`upload_id` single-flight guard |
| `Response` | utility | `src/utils/Response.hpp` | Converts `Result<T>` into uniform `{code,message,data}` JSON |
| `Application` | Qt bridge | `clients/desktop/src/app/Application.*` | Owns/injects managers into QML context and wires auth/session signals |
| `RequestFactory` | desktop network | `clients/desktop/src/network/RequestFactory.hpp` | Auth-domain-aware headers; owner and visitor tokens never mix |

## CONVENTIONS

- C++23, CMake + vcpkg; root backend and `clients/desktop/` are separate CMake projects.
- Formatting is `.clang-format`: Google-based, 4-space indent, `ColumnLimit: 0`, `PointerAlignment: Left`, `AccessModifierOffset: -4`.
- `.clangd` enables broad clang-tidy groups and removes noisy checks such as magic numbers, identifier length, trailing-return modernization.
- Naming: classes/structs `PascalCase`; backend public methods usually `PascalCase`; private members `m_` + `snake_case`; constants mostly `UPPER_SNAKE_CASE` or Qt-style `kName`.
- Services return `drogon::Task<Result<T>>` / `Result<T>`; business errors are `std::expected` values, not exceptions.
- DTOs are contracts: request DTOs expose `static FromRequest(...) -> Result<DTO>`; response DTOs expose `ToJson()`.
- Controllers inherit `drogon::HttpController<>`, register routes in `METHOD_LIST_BEGIN/END`, delegate to services, and use `Response::FromResult()`.
- Drogon ORM model `.hpp` files are generated-style and large; put custom logic in `.cpp`, not headers.
- Third-party/backend framework includes are centralized through `src/utils/Pch.hpp` for the main target.

## ANTI-PATTERNS (THIS PROJECT)

- Do not put business logic in controllers.
- Do not throw/catch for business flow; return `Result<T>` / `ErrorInfo`.
- Do not hand-edit generated ORM `.hpp` files for custom behavior.
- Do not manually parse JSON in controllers when a DTO belongs in `src/dtos/`.
- Do not use `upload_tasks.uploaded_chunks` for new code; it is deprecated compatibility storage. Use `upload_task_chunks`.
- Do not make desktop warnings fatal; `clients/desktop/CMakeLists.txt` says advisory warnings are target-scoped and never `-Werror`.
- Do not store production passwords/secrets in `config.json`; production uses environment variables and `DISK_SECURE_MODE=true`.
- Do not expose Redis/internal services on public networks; deployment docs require private/internal addresses.
- Do not add parallel replacement code when existing code can be corrected; prefer modifying current code paths and deleting dead/obsolete code to achieve the intended behavior.

## UNIQUE STYLES

- Global auth is configured through Drogon `GlobalFilters` in `config.json`, with regex exempt paths for public auth/share/health routes.
- `StorageMgr::SetInstance()` happens once at startup; services retrieve the active `IFileStorage` through the manager.
- Upload assembly is fail-fast: `AssemblyWorkerPool` rejects when max concurrency or same-`upload_id` single-flight is hit; it does not queue.
- Python integration tests use PEP 723 headers and `uv run`; helpers can auto-start the backend and write evidence under `.sisyphus/evidence/`.
- Desktop QML treats three file views (`myfiles`, `shared`, `trash`) as view modes inside `DriveBrowserPage`, not separate StackView pages.
- Desktop has two auth domains: owner JWT (`Authorization: Bearer`) and visitor share token (`X-Share-Token`); `RequestFactory` enforces separation.

## COMMANDS

```bash
# Backend configure/build
cmake --preset linux-debug-clang
cmake --build --preset linux-debug-clang

# Backend tests
ctest --preset linux-debug-clang -V
ctest --preset linux-debug-clang -R FileService -V
./build/linux-debug-clang/test/disk-test --gtest_list_tests
uv run test/integration/test_auth_flow.py

# Run backend
JWT_SECRET=dev-only-jwt-secret-key-change-in-production-2024 ./build/linux-debug-clang/src/disk

# Desktop configure/build from clients/desktop when needed
cmake -S clients/desktop -B clients/desktop/build
cmake --build clients/desktop/build

# Dev loop
bash scripts/auto-build.sh

# Format source/test C++
find src test clients/desktop/src clients/desktop/tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# DB init
mysql -u root -p < sql/init.sql
```

## NOTES

- `CMakePresets.json` contains a dev-only JWT secret; production must override with a secure random value of at least 32 chars.
- Integration tests are serial (`RUN_SERIAL TRUE`) because they share server/database/Redis state.
- `FileService.cpp`, `FileDto.hpp`, `DriveBrowserPage.qml`, and `TransferManager.cpp` are the largest hotspots; read nearby tests/docs before edits.
- Docs are mostly Chinese. Backend design docs live in `docs/design/`; desktop authority docs live in `docs/desktop/00-07`.
