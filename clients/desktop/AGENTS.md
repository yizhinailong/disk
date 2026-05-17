# DESKTOP CLIENT KNOWLEDGE

## OVERVIEW

`clients/desktop/` is an independent Qt6/C++23 + QML desktop client. It consumes the backend REST API and does not change backend behavior or dependencies.

## STRUCTURE

```text
clients/desktop/
├── CMakeLists.txt       # Qt6 project; disk-desktop + tests subdir
├── src/
│   ├── app/             # Application context injection, ShellController navigation
│   ├── auth/            # AuthService, SessionStore, owner/visitor session managers
│   ├── managers/        # Drive/Profile/Transfer/Share/Trash managers exposed to QML
│   ├── models/          # QAbstractItemModel/ListModel data contracts
│   └── network/         # NetworkClient, RequestFactory, ErrorAdapter
├── qml/
│   ├── Main.qml         # shell router
│   ├── shells/          # AuthShell, OwnerShell, VisitorShell
│   ├── pages/           # full pages: login, drive, transfer, settings, share, splash
│   └── components/      # themes, panels, owner widgets, drive subviews
└── tests/
    ├── unit/            # Qt Test C++ suites
    ├── quick/           # Qt Quick Test QML suites
    ├── fixtures/json/   # canned API responses
    └── helpers/         # MockNetworkAccessManager, MockReplyFactory, TestJsonLoader
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add QML page | `qml/pages/`, `qml/shells/OwnerShell.qml` | Pages either replace StackView or are embedded view modes |
| Add reusable UI | `qml/components/` | Theme constants live in `WorkspaceTheme.qml` / `AuthTheme.qml` |
| Change file browser | `qml/pages/DriveBrowserPage.qml`, `qml/components/drive/` | `myfiles/shared/trash` are view modes inside PAGE-DRIVE |
| Change auth/session | `src/auth/`, `src/app/ShellController.*` | Owner and visitor sessions are mutually exclusive domains |
| Change API calls | `src/managers/`, `src/network/RequestFactory.*` | Managers are QML boundary; RequestFactory owns auth headers |
| Change transfer logic | `src/managers/TransferManager.*`, task models | Upload/download state machines; largest desktop C++ hotspot |
| Add desktop unit test | `tests/unit/<domain>/test_*.cpp` | Qt Test, not GoogleTest |
| Add QML behavior test | `tests/quick/<domain>/tst_*.qml` | Qt Quick Test; often reads QML source as contract evidence |
| Add fixture | `tests/fixtures/json/{auth,models,transfers}/` | Keep feature-grouped canned responses |

## ARCHITECTURE

| Layer | Contract |
|-------|----------|
| `Application` | Owns `NetworkClient`, `RequestFactory`, services/managers/models; injects context properties into QML |
| `ShellController` | Exposes `currentShell` and `pageState`; routes splash/auth/owner/visitor states |
| `SessionStore` | Owns owner + visitor session managers; only one auth domain active at a time |
| `RequestFactory` | `Public` no auth, `Owner` adds `Authorization`, `Visitor` adds `X-Share-Token`; never mix tokens |
| Managers | `Q_INVOKABLE` methods for QML; own QML-facing models as `Q_PROPERTY` |
| Models | `QAbstractListModel` / `QAbstractItemModel`; normalize backend JSON into QML roles |
| QML | Declarative UI + light state wiring; business/network logic stays in C++ managers |

## STATE MACHINES

- Owner session: `LoggedOut → Authenticating → Active → Refreshing → Active/ReauthRequired → LogoutPending → LoggedOut`.
- Visitor session: `Idle → Unverified → Verifying → Active → ReverifyRequired/Closed`.
- Upload: `Queued → Hashing → Initializing → InstantUploaded/Resuming/Uploading → Completing → Completed`, plus cancel/fail/expire branches.
- Download: `Idle → FetchingMetadata → Ready → TransferringFull/TransferringRange/Paused → Completed`, plus retry/cancel/fail branches.

## CONVENTIONS

- C++ class names `PascalCase`; private members `m_` + snake_case; Qt signals/slots follow Qt naming where needed.
- QML files/components `PascalCase.qml`; QML properties/functions `camelCase`.
- Use `Q_INVOKABLE` for QML-callable manager methods and `Q_PROPERTY(... CONSTANT)` for owned models.
- QML pages use `Connections` to bind to C++ signals such as `apiError`, `paginationLoaded`, `operationSuccess`.
- `OwnerShell.qml` has two nav categories: file view modes (`myfiles`, `shared`, `trash`) and independent pages (`transfers`, `settings`).
- `DriveBrowserPage.qml` folder navigation changes `currentFolderId` in place and refreshes tree/list/breadcrumb; it does not push a new page.
- Desktop warning flags are advisory and target-scoped; never introduce `-Werror`.

## TESTS

| Target | Scope | Command |
|--------|-------|---------|
| `desktop-unit-tests` | C++ Qt Test suites for auth/models/managers/network/navigation/transfers | `ctest --preset linux-debug-clang -R desktop-unit-tests -V` |
| `desktop-quick-tests` | QML shells/pages/components/navigation behavior | `ctest --preset linux-debug-clang -R desktop-quick-tests -V` |

Special test behavior: fixtures are copied to the build dir; quick tests run `-platform offscreen`; `QML_XHR_ALLOW_FILE_READ=1` supports source contract checks; `DESKTOP_QML_EVIDENCE_DIR` stores QML evidence.

## ANTI-PATTERNS

- Do not add platform-specific system calls; desktop is Linux-first and Windows-ready via Qt APIs.
- Do not invent backend APIs from desktop; align with `docs/design/02-API接口设计.md`.
- Do not mix owner and visitor credentials in one request.
- Do not put network/business logic directly in QML.
- Do not split shares/trash into independent owner pages unless desktop docs change; current model keeps them in `DriveBrowserPage` view modes.
- Do not make desktop compiler warnings fatal.

## HOTSPOTS

- `qml/pages/DriveBrowserPage.qml`: main file browser and mutation dialogs; largest QML hotspot.
- `src/managers/TransferManager.cpp`: upload/download state machines and active reply tracking.
- `qml/shells/OwnerShell.qml`: navigation rail, page headers, view-mode routing.
- `tests/quick/pages/tst_drive_browser_navigation.qml`: contract tests for navigation, refresh, breadcrumb, state behavior.
