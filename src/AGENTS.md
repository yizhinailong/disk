# BACKEND SOURCE KNOWLEDGE

## OVERVIEW

`src/` builds the Drogon backend executable `disk`: HTTP controllers, DTO contracts, coroutine services, ORM models, filters, storage, and utilities.

## STRUCTURE

```text
src/
├── main.cpp          # startup, config, TokenService, StorageMgr, scheduled tasks
├── controllers/      # route handlers; no business logic
├── dtos/             # request parsing/validation + response JSON contracts
├── filters/          # JWT/share/admin/rate-limit middleware
├── models/           # Drogon ORM models; generated-style headers
├── services/         # business logic; drogon::Task<Result<T>>
├── storage/          # IFileStorage, LocalFileStorage, assembly concurrency
└── utils/            # ConfigMgr, ErrorCode, Response, hashes, PCH, batch helpers
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add route | `controllers/*Controller.hpp/.cpp` | Register in `METHOD_LIST_BEGIN`; convert service result only |
| Add request/response shape | `dtos/*Dto.hpp` | `FromRequest()` validates; `ToJson()` serializes |
| Add domain logic | `services/*Service.hpp/.cpp` | Return `drogon::Task<Result<T>>`; keep controller thin |
| Add auth/public access | `filters/`, `config.json` | GlobalFilters exemptions are regex strings in config |
| Add file storage operation | `storage/IFileStorage.hpp`, `LocalFileStorage.*` | Storage layer never owns HTTP, DB, auth decisions |
| Add shared error | `utils/ErrorCode.hpp` | Keep code ranges: 10xxx common, 40xxx auth, 50xxx file, 60xxx share, 70xxx Redis, 80xxx admin |
| Add config field | `utils/ConfigMgr.*`, `config.json` | Validate secure/prod behavior |

## CODE MAP

| Area | Key files | Contract |
|------|-----------|----------|
| File domain | `FileService.cpp`, `FileDto.hpp`, `FileController.*` | Upload/chunk/complete/cancel, list/detail/download, rename/move/copy/delete/search |
| Auth domain | `AuthService.*`, `TokenService.*`, `JwtAuthFilter.*` | Login lockout, access/refresh JWT, blacklist/revocation, Redis-backed state |
| Share domain | `ShareService.*`, `ShareDto.hpp`, `ShareAuthFilter.*` | Owner share CRUD + public access/browse/download via share token |
| Trash domain | `TrashService.*`, `TrashDto.hpp` | Soft delete, restore with conflict naming, permanent delete, clear |
| Admin domain | `AdminService.*`, `AdminDto.hpp`, `AdminAuthFilter.*` | User status/role changes and admin safety checks |
| Storage | `IFileStorage.hpp`, `LocalFileStorage.*`, `AssemblyWorkerPool.hpp` | Content-addressed paths, temp chunks, promotion, Range-ready reads |
| Utilities | `ErrorCode.hpp`, `Response.hpp`, `BatchUtils.hpp`, `RedisKeyPrefix.hpp`, `StageTimer.hpp` | Cross-cutting contracts used by multiple services |

## CONVENTIONS

- Use `#pragma once`; most headers include Doxygen `@file/@brief` blocks.
- Public service methods are usually `[[nodiscard]] auto Method(...) -> drogon::Task<Result<T>>`.
- Business failures use `std::unexpected(ErrorInfo(...))`; do not convert them to HTTP until the controller/response boundary.
- Controllers should call `DTO::FromRequest(request)` before service work; avoid duplicated parsing/validation.
- Use `drogon::orm::Mapper`/`CoroMapper` in services; never issue raw DB queries from controllers.
- For batch DB paths, prefer `BatchUtils` helpers for chunking and `IN` placeholders.
- Use `RedisKeyPrefix` for new Redis keys; avoid ad hoc key strings.
- Keep `ConfigMgr::LoadConfig()` after `drogon::app().loadConfigFile()` and before reading custom config values.

## ANTI-PATTERNS

- Do not add controller business logic.
- Do not edit ORM `.hpp` files for custom behavior; add methods/logic in `.cpp` or services.
- Do not use exceptions for expected validation/auth/domain failures.
- Do not bypass `StorageMgr` after startup unless explicitly injecting a test/storage seam.
- Do not queue upload assembly work in `AssemblyWorkerPool`; current design rejects over-capacity or duplicate `upload_id` immediately.
- Do not introduce public endpoints without updating `config.json` GlobalFilters exemptions and tests.

## HOTSPOTS

- `services/FileService.cpp` (~2.8k lines): highest-risk backend file; read unit + integration tests before changes.
- `dtos/FileDto.hpp` (~1.7k lines): all file API contract types; changes here ripple into tests and desktop.
- `services/ShareService.cpp` and `services/TrashService.cpp`: large domain services with batch/conflict semantics.
- `utils/ErrorCode.hpp`: central HTTP status/message mapping; missing entries cause inconsistent API responses.

## VERIFY

```bash
cmake --build --preset linux-debug-clang
ctest --preset linux-debug-clang -R '<DomainOrSuite>' -V
uv run test/integration/test_<scenario>.py
```

For auth/password/hash changes, confirm libsodium-dependent tests still run through `test/main.cpp` global environment.
