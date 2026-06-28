# Spec Implementation Gap Audit

Change: `audit-spec-implementation-gap`

This report is a point-in-time audit of the main OpenSpec capabilities against implementation and verification evidence in the current repository. It separates implementation coverage from verification coverage so follow-up work can distinguish missing behavior from missing tests or documentation evidence.

## Status model

Implementation status:

- `implemented`: direct source or documentation evidence satisfies the requirement.
- `partial`: some required behavior exists, but scope, edge cases, or one client/backend path is incomplete.
- `not-implemented`: no implementation evidence found in the audited scope.
- `ambiguous`: the requirement or implementation evidence is not clear enough to judge.
- `not-audited`: intentionally not assessed in this report.

Verification status:

- `verified`: automated test, executed check, or strong evidence verifies the behavior.
- `partial`: some verification exists, but important paths or edge cases are unverified.
- `missing`: no verification evidence found.
- `ambiguous`: verification evidence or expected coverage is unclear.
- `not-audited`: intentionally not assessed in this report.

## Capability inventory

All main capabilities under `openspec/specs/` are represented:

| Capability | Overall implementation | Overall verification | Primary gap theme |
|---|---|---|---|
| `admin-operations` | mostly implemented | mostly verified | Last-administrator protection should cover every destructive/admin state transition. |
| `api-contract` | mostly implemented | mostly verified | Request trace propagation and end-to-end binary streaming need stronger tests. |
| `architecture-decisions` | mostly not implemented | missing | Durable ADR documents are absent. |
| `client-integration` | partial to implemented | partial | Cross-client contract consistency is not directly tested. |
| `deployment-operations` | partial | mostly missing | Operational runbooks exist, but execution evidence is missing. |
| `desktop-client-experience` | partial to implemented | partial/missing | Several UI behaviors are documented but not runtime-verified. |
| `documentation-governance` | implemented as docs | mostly missing | Governance exists but lacks executed validation artifacts. |
| `file-namespace` | partial to implemented | partial | Backend/test evidence is strongest for folders and weaker for listing/search/details. |
| `file-transfer` | partial to implemented | partial/verified | Reservation, instant upload, and retry/finalization semantics need stronger proof. |
| `identity-and-session` | implemented | partial to verified | Registration and logout need fuller HTTP-path verification. |
| `observability` | partial to implemented | partial | Trace visibility and background maintenance visibility are weak. |
| `persistence-design` | partial/not implemented | missing | Spec is placeholder-like and lacks schema/index/constraint contract. |
| `runtime-configuration` | partial/ambiguous | partial/missing | PostgreSQL/Redis wiring and public route exemptions are not explicit enough. |
| `sharing` | implemented with one ambiguity | mostly verified | Token scope needs explicit negative tests. |
| `trash-lifecycle` | mostly implemented | mostly verified | Expiry cleanup policy/implementation is unclear. |
| `validation-and-performance` | partial to implemented as plans | partial/missing | Benchmark, compatibility, and validation evidence are not attached. |

## Detailed audit matrix

### `api-contract`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Uniform JSON API Envelope | implemented | verified | `src/utils/Response.hpp:60-116`; `test/utils/Response_test.cpp:20-51` | Low. |
| Error Code Contract | implemented | verified | `src/utils/ErrorCode.hpp:31-195`; `test/filters/JwtAuthFilter_test.cpp:352-435`; `test/filters/ShareAuthFilter_test.cpp:119-170`; `test/services/LoginRateLimit_test.cpp:22-112` | Low. |
| Pagination Envelope | implemented | verified | `src/utils/Response.hpp:22-56,101-106`; `test/dtos/FileDto_test.cpp:1523-1546`; `test/services/ShareService_test.cpp:647-657`; `test/dtos/AdminDto_test.cpp:755-777` | Low. |
| Authentication Header Contract | implemented | verified | `src/filters/JwtAuthFilter.cpp:24-85`; `src/filters/ShareAuthFilter.cpp:20-66`; `test/integration/test_auth_flow.py:158-203`; `test/filters/ShareAuthFilter_test.cpp:186-240` | Low. |
| Binary Download Contract | implemented | partial | `src/controllers/DownloadResponder.cpp:36-213`; `test/controllers/DownloadResponder_test.cpp:274-360` | Need stronger end-to-end streamed-body verification. |
| Request Trace Propagation | partial | missing | `src/main.cpp:73-80` adds `X-Request-Id` from request attributes | Needs direct tests and clarity on whether IDs are generated or only propagated. |

### `identity-and-session`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| User Registration | implemented | missing | `src/controllers/AuthController.cpp:19-49`; `src/services/AuthService.cpp:45-123` | Add registration HTTP-path tests, including default storage initialization. |
| Password Protection | implemented | partial | `src/services/AuthService.cpp:81-99`; `src/services/UserService.cpp:96-145`; `test/utils/PasswdHash_test.cpp` found by search | Add persistence-level assertion that plaintext is never stored. |
| User Login | implemented | verified | `src/services/AuthService.cpp:126-212`; `test/integration/test_auth_flow.py:48-75` | Low. |
| Access Token Authentication | implemented | verified | `src/filters/JwtAuthFilter.cpp:24-85`; `test/integration/test_auth_flow.py:158-203`; `test/filters/JwtAuthFilter_test.cpp:74-279` | Low. |
| Refresh Token Lifecycle | implemented | verified | `src/services/TokenService.hpp:182-223,248-333`; `src/services/TokenService.cpp:225-327,611-667`; `src/services/AuthService.cpp:215-271`; `test/integration/test_refresh_token.py:83-189`; `test/services/TokenService_test.cpp:45-105,155-176,455-491` | Low. |
| Logout Revocation | implemented | partial | `src/services/AuthService.cpp:284-331`; `src/services/TokenService.cpp:329-381,631-667`; `test/services/TokenServiceRevocation_test.cpp:84-183,253-364` | Add full HTTP logout-to-rejection integration test. |
| Account Protection | implemented | verified | `src/services/AuthService.cpp:126-185,372-446`; `test/services/LoginRateLimit_test.cpp:22-112`; `test/integration/test_login_rate_limit.py:47-123` | Low. |

### `runtime-configuration`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Configuration Loading | implemented | partial | `src/main.cpp:24-29`; `src/utils/ConfigMgr.cpp:21-110`; `test/utils/ConfigMgr_test.cpp:319-399` | Startup wiring not fully covered. |
| Secure Configuration Validation | implemented | verified | `src/main.cpp:30-36`; `src/utils/ConfigMgr.cpp:194-242`; `test/utils/ConfigMgr_test.cpp:161-273` | Low. |
| Database Connectivity Configuration | ambiguous | missing | `src/utils/ConfigMgr.cpp:89-109,244-249`; `src/services/AuthService.cpp:35-42`; `src/services/UserService.cpp:27-30` | PostgreSQL client wiring from settings is not explicit in audited startup code. |
| Redis Connectivity Configuration | ambiguous | missing | `src/utils/ConfigMgr.cpp:102-109,186-192`; `src/services/AuthService.cpp:35-42`; `src/services/TokenService.cpp:285,349-381,631-667` | Redis connection configuration path is not explicit enough. |
| File Storage Configuration | implemented | partial | `src/main.cpp:44-61`; `src/utils/ConfigMgr.cpp:21-88,138-174`; `test/utils/ConfigMgr_test.cpp:319-399` | Add end-to-end storage implementation initialization coverage. |
| Background Task Registration | implemented | missing | `src/main.cpp:67-71`; `src/services/TokenService.cpp:407-435` | Add boot-time scheduler registration tests. |
| Public Route Exemptions | ambiguous | missing | Route-specific auth filters in `src/filters/JwtAuthFilter.cpp` and `src/filters/ShareAuthFilter.cpp` | Canonical public route list and exemption tests are missing. |

### `architecture-decisions`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Architecture decision records | not-implemented | missing | Search found no durable ADR documents beyond spec/change stubs | Create ADR home and required records. |
| PostgreSQL database-selection decision | not-implemented | missing | No ADR containing PostgreSQL rationale found | High documentation-governance risk. |
| Download streaming API decision | partial | missing | `src/controllers/DownloadResponder.cpp:25-33,78-133`; `test/controllers/DownloadResponder_test.cpp:274-360` | Rationale exists inline, not as ADR with alternatives/rollback/revisit criteria. |
| io_uring feasibility decision | not-implemented | missing | Search found no io_uring decision record | Add no-go/feasibility ADR if still desired. |
| Behavior-preserving decision governance | not-implemented | missing | No durable decision records or linked implementation-change trail found | Architectural intent can be lost. |

### `file-namespace`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| File and folder listing | partial | missing | `clients/disk-web/src/api/file.ts:54-56`; `clients/disk-web/src/types/drive.ts:23-31`; search found only client API | Backend handler and pagination/type-field tests not evidenced. |
| File and folder details | partial | missing | `clients/disk-web/src/api/file.ts:58-63`; `clients/disk-web/src/types/drive.ts:35-48` | Backend detail handler/test evidence not found. |
| Folder creation | implemented | verified | `src/services/FolderService.cpp:43-120`; `test/integration/test_folder_lifecycle.py:146-185,343-347` | Low. |
| Rename items | partial | verified | `src/services/FolderService.cpp:123-279`; `clients/disk-web/src/api/file.ts:83-85`; `test/integration/test_folder_lifecycle.py:365-390`; `test/integration/test_file_mutation_ops.py:167-198` | File rename backend not directly surfaced. |
| Move items | partial | verified | `clients/disk-web/src/api/file.ts:87-89`; `test/integration/test_folder_lifecycle.py:416-452` | File move backend and invalid target semantics need stronger proof. |
| Copy items | partial | verified | `clients/disk-web/src/api/file.ts:91-93`; `test/integration/test_copy_delete_atomicity.py:172-342` | Quota/content-reference semantics not directly evidenced. |
| Search namespace | partial | missing | `clients/disk-web/src/api/file.ts:99-100`; `clients/disk-web/src/types/drive.ts:59-69`; search found only client API | Backend search handler/tests not found. |
| Folder navigation metadata | implemented | verified | `src/services/FolderService.cpp:356-415,483-534`; `clients/disk-web/src/api/folder.ts:14-19` | Low. |

### `file-transfer`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Upload initialization | partial | partial | `clients/disk-web/src/api/file.ts:24-26`; `test/services/UploadPath_test.cpp:165-205`; `test/integration/test_upload_flow.py:99,318,354` | Missing proof for all validation branches. |
| Upload storage reservation | ambiguous | missing | `clients/disk-tui/internal/client/file.go:263-345`; `test/services/UploadPath_test.cpp:131-205` | Reservation/release accounting not directly evidenced. |
| Instant upload | partial | missing | `clients/disk-tui/internal/client/file.go:275-284` | Server-side dedupe and tests not found. |
| Resumable chunk upload | partial | verified | `clients/disk-tui/internal/client/file.go:289-345`; `clients/disk-web/src/api/file.ts:28-44`; `test/integration/test_upload_flow.py:221,354` | Server-side chunk persistence not directly inspected. |
| Upload completion | partial | verified | `clients/disk-web/src/api/file.ts:46-47`; `test/integration/test_upload_flow.py:99,221,354` | Integrity/retry preservation semantics need stronger evidence. |
| File download metadata | implemented | verified | `clients/disk-web/src/api/file.ts:62-63`; `clients/disk-web/src/composables/useDownload.ts:14-18`; `test/integration/test_download_flow.py:232,274,310` | Low. |
| Ranged file download | implemented | verified | `clients/disk-web/src/api/file.ts:66-80`; `clients/disk-web/src/composables/useDownload.ts:72-112`; `test/integration/test_download_flow.py:232,274,310,355,392,430` | Low. |

### `sharing`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Share creation | implemented | verified | `src/services/ShareService.cpp:92-243`; `clients/disk-web/src/api/share.ts:20-22`; `test/integration/test_share_browse.py:205`; `test/integration/test_share_management.py:167` | Low. |
| External share identifier | implemented | verified | `src/services/ShareService.cpp:231-239`; `src/dtos/ShareDto.hpp:381-409`; `clients/disk-web/src/api/share.ts:28-45` | Low. |
| Owner share management | implemented | verified | `src/services/ShareService.cpp:245-363`; `clients/disk-web/src/api/share.ts:24-38`; `test/integration/test_share_management.py:207,241,340` | Low. |
| Public share access | implemented | verified | `src/services/ShareService.cpp:650-716`; `clients/disk-web/src/api/share.ts:40-65`; `test/integration/test_share_browse.py:250,288,314`; `test/integration/test_share_management.py:308,405,432` | Low. |
| Share token scope | ambiguous | partial | `src/services/ShareService.cpp:693-716`; `clients/disk-web/src/api/share.ts:47-65` | Add negative tests proving token rejection across shares and permissions. |
| Browse shared content | implemented | verified | `clients/disk-web/src/api/share.ts:47-54`; `test/integration/test_share_browse.py:288,314` | Low. |
| Download shared content | implemented | verified | `clients/disk-web/src/api/share.ts:56-65`; `test/integration/test_download_flow.py:355,392,430` | Low. |

### `trash-lifecycle`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Soft delete | partial | verified | `clients/disk-web/src/api/file.ts:95-96`; `clients/disk-web/src/api/trash.ts:12-25`; `test/integration/test_trash_lifecycle.py:237,247,301`; `test/integration/test_file_delete_stress.py:150,201,271` | Storage-retention-on-trash not explicitly proven. |
| Trash listing | implemented | verified | `src/services/TrashService.cpp:251-302`; `clients/disk-web/src/api/trash.ts:12-14`; `test/integration/test_trash_lifecycle.py:237,247,301,503` | Low. |
| Restore trash items | partial | verified | `src/services/TrashService.cpp:322-423`; `src/services/TrashService.hpp:163-164`; `clients/disk-web/src/api/trash.ts:16-18`; `test/integration/test_trash_lifecycle.py:319,365,402` | Original-location fallback and deterministic conflict renaming need clearer proof. |
| Permanent delete | implemented | verified | `src/services/TrashService.cpp:642-803`; `test/integration/test_trash_lifecycle.py:434`; `test/integration/test_file_delete_stress.py:341` | Low. |
| Empty trash | implemented | verified | `src/services/TrashService.cpp:642-803`; `test/integration/test_trash_lifecycle.py:503`; `test/integration/test_file_delete_stress.py:341` | Low. |
| Expiry cleanup | ambiguous | missing | `src/services/TrashService.cpp:251-302` exposes `expires_at`; searches for cleanup/retention found no job/test | Define and implement retention cleanup or explicitly move it out of scope. |

### `persistence-design`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| PostgreSQL persistence baseline | not-implemented | missing | `openspec/specs/persistence-design/spec.md` is placeholder-like; models exist but no contract | Replace placeholder with real persistence contract. |
| Relational schema contracts | partial | missing | `src/models/Shares.hpp:42-66`; `src/models/ShareFiles.hpp:42-58`; `src/models/UploadTasks.hpp`; `src/models/OperationLogs.hpp` | Constraints, FK/index rules, and soft-delete policy are not documented. |
| Upload task lifecycle persistence | partial | missing | `src/models/UploadTasks.hpp:92-98`; upload tests exist | Concurrency, reservation bytes, expiry, finalization fields not validated. |
| Storage accounting and reference counts | partial | partial | `src/services/TrashService.cpp:642-803`; `test/integration/test_trash_lifecycle.py:434,503` | Add quota/refcount/blob cleanup edge-case tests. |
| Search and query indexing | not-implemented | missing | No index/migration documentation found | Add lookup/search/cleanup/share index strategy. |
| PostgreSQL persistence constraints / canonical source linkage | not-implemented | missing | Spec lacks canonical architecture decision link | Link to ADR once created. |

### `client-integration`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| REST Client Compatibility | implemented | partial | `clients/disk-tui/internal/client/client.go:57-75,335-364`; `clients/disk-web/src/api/client.ts:112-219`; `clients/desktop/src/network/NetworkClient.hpp:19-66` | No cross-client contract test. |
| Owner Authentication Domain | implemented | verified | `clients/desktop/src/network/RequestFactory.cpp:12-44`; `clients/desktop/tests/unit/network/test_request_factory.cpp:12-105`; `clients/desktop/tests/unit/auth/test_owner_session.cpp:28-313` | Low. |
| Visitor Share Domain | implemented | verified | `clients/desktop/src/network/RequestFactory.cpp:24-44`; `clients/desktop/src/auth/VisitorSessionManager.hpp:20-88`; `clients/desktop/tests/unit/auth/test_visitor_session.cpp:28-160` | Low. |
| Token Refresh Integration | implemented | verified | `clients/disk-tui/internal/client/auth.go:47-57`; `clients/disk-tui/internal/client/client.go:335-364`; `clients/desktop/src/auth/OwnerSessionManager.hpp:36-116`; `clients/desktop/tests/unit/auth/test_owner_session.cpp:68-168`; `test/integration/test_refresh_token.py` found | Desktop UI replay path not end-to-end tested. |
| Client Upload Workflow | partial | partial | `clients/disk-tui/internal/client/client.go:57-75,335-364`; `clients/disk-web/src/api/file.ts:24-50`; `test/integration/test_upload_flow.py` found | Cancel/retry semantics not proven across all clients. |
| Cross-Client Behavior Consistency | partial | missing | `clients/desktop/src/managers/AdminManager.cpp:79-100,357-864`; `test/integration/test_admin_flow.py:285-771` | Add dedicated cross-client error contract tests. |

### `desktop-client-experience`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Desktop shell separation | implemented | verified | `clients/desktop/src/app/ShellController.cpp:9-191`; `clients/desktop/src/network/RequestFactory.cpp:12-44`; desktop auth/session tests | Low. |
| Desktop platform and client integration constraints | implemented | missing | `docs/desktop/00-桌面客户端系统概述与文档治理.md:43-80`; `clients/desktop/src/network/NetworkClient.hpp:19-24`; `clients/desktop/tests/quick/quick_test_main.cpp:945-1027` | Windows build/runtime not evidenced. |
| Owner explorer information architecture | partial | partial | `docs/desktop/01-信息架构与功能视图.md:31-76,146-233,352-383`; `clients/desktop/src/app/ShellController.cpp:44-180` | Runtime UI evidence incomplete. |
| Desktop object taxonomy | implemented | missing | desktop docs mapping object taxonomy and page/component mappings | No automated enforcement evidence. |
| Desktop navigation and state model | partial | partial | `clients/desktop/src/app/ShellController.cpp:18-191`; desktop auth/session tests; `docs/desktop/03-状态模型与导航模型.md` | Admin/view-switch/skeleton cases remain planning. |
| Desktop interaction and layout contracts | partial | partial | `docs/desktop/02-页面布局与交互规范.md`; `docs/desktop/04-组件页面与实现映射.md` | Search/grid/drag-drop/skeleton/multi-select extensions still planned. |
| Desktop implementation traceability | implemented | missing | `docs/desktop/04-组件页面与实现映射.md`; desktop quick/unit tests | No executed docs-validation artifact. |
| Admin desktop experience | partial | partial | `clients/desktop/src/managers/AdminManager.cpp:10-863`; `clients/desktop/tests/unit/managers/test_admin_manager.cpp:23-886`; `docs/desktop/08-管理员功能设计.md` | UI confirmation/admin-shell routing not directly UI-tested. |
| Chinese desktop UI terminology | partial | missing | `docs/desktop/07-中文UI术语表.md`; layout docs | No runtime i18n test coverage. |

### `admin-operations`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Administrator Authorization | implemented | verified | `src/filters/AdminAuthFilter.cpp:18-46`; `src/controllers/AdminController.cpp:17-417`; `test/integration/test_admin_flow.py:504-537` | Low. |
| User Administration | implemented | verified | `src/controllers/AdminController.cpp:17-417`; `clients/desktop/src/managers/AdminManager.cpp:105-237,357-438`; `clients/desktop/tests/unit/managers/test_admin_manager.cpp:43-335` | Low. |
| Administrator Self-Protection | implemented | verified | `src/controllers/AdminController.cpp:77-163,211-244`; desktop admin tests; `test/integration/test_admin_flow.py:455-499` | Low. |
| Last Administrator Protection | partial | partial | `src/controllers/AdminController.cpp:121-163,211-244`; desktop admin tests; `test/integration/test_admin_flow.py:481-499` | Delete/lock/disable paths need explicit evidence. |
| Administrative Share Moderation | implemented | verified | `src/controllers/AdminController.cpp:264-357`; desktop AdminManager; `test/integration/test_admin_flow.py:540-585` | Low. |
| Administrative System Statistics | implemented | verified | `src/controllers/AdminController.cpp:247-415`; desktop AdminManager; `test/integration/test_admin_flow.py:588-641,665-713`; desktop tests | Low. |

### `deployment-operations`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Secure runtime configuration | partial | partial | `docs/design/05-部署运维指南.md`; `test/utils/ConfigMgr_test.cpp`; `test/filters/JwtAuthFilter_test.cpp` | Startup rejection behavior not directly evidenced. |
| Deployment prerequisites and build process | implemented | missing | `docs/design/05-部署运维指南.md:43-213` | No execution evidence. |
| Database and cache operations | partial | partial | deployment guide; `src/services/HealthService.cpp:31-127`; `src/services/SystemService.cpp:35-106`; Redis/system info tests | Full deployment workflow not exercised. |
| Incremental database migration procedure | partial | missing | `docs/design/05-部署运维指南.md:315-541` | No migration/rollback run evidence. |
| Service management and hardening | partial | missing | deployment guide systemd/hardening sections | No service-install/hardening evidence. |
| HTTPS and reverse proxy operations | partial | missing | deployment guide nginx/TLS sections | No nginx/TLS execution evidence. |
| Monitoring, logging, backup, restore, troubleshooting | partial | partial | deployment guide; health/system/log controllers and tests | Backup/restore/troubleshooting are documentation-only. |
| Upgrade and rollback operations | partial | missing | deployment guide upgrade/rollback sections | No upgrade/rollback execution evidence. |

### `observability`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Health Check | implemented | verified | `src/services/HealthService.cpp:21-127`; `src/controllers/HealthController.cpp:16-40`; desktop HealthManager; `test/integration/test_system_info.py:47-204` | Low. |
| System Information | implemented | verified | `src/services/SystemService.cpp:25-106`; `src/controllers/SystemController.cpp:16-45`; desktop AdminManager; `test/integration/test_system_info.py:63-204`; `test/integration/test_admin_flow.py:613-641` | Low. |
| Operation Logs | implemented | partial | `src/services/OperationLogService.hpp:22-105`; `src/controllers/OperationLogController.cpp:16-75`; desktop AdminManager/tests | Direct service test evidence not fully audited. |
| Request Trace Visibility | not-implemented | missing | `src/utils/LogHelper.hpp:49-58`; `src/filters/AdminAuthFilter.cpp:43-46` only show trace-level logging | Add trace-id propagation/response/log correlation behavior. |
| Background Maintenance Visibility | partial | missing | test/design docs mention cleanup/system coverage | Scheduler/runtime evidence missing. |

### `documentation-governance`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| OpenSpec as future requirement authority | implemented | missing | desktop governance docs | No enforcement artifact. |
| Legacy documentation status | implemented | missing | desktop governance and migration docs | No archive validation evidence. |
| Behavior-preserving documentation governance | implemented | missing | desktop governance and validation-plan docs | No runtime-change guard evidence. |
| Capability-oriented documentation structure | implemented | missing | desktop governance/information architecture docs | No audit artifact proving migration completeness. |
| Status labels and evidence discipline | implemented | partial | desktop governance/validation/mapping docs | Validation plan exists but executed evidence is missing. |
| Traceable source coverage | implemented | missing | desktop governance, information architecture, component mapping docs | Source-area traceability not externally validated. |

### `validation-and-performance`

| Requirement | Implementation | Verification | Evidence | Gap / risk |
|---|---|---|---|---|
| Multi-level validation coverage | implemented | partial | `docs/design/04-系统测试计划.md`; `docs/design/06-单元测试用例.md`; `docs/design/07-压力测试.md` | Desktop docs checks are planned, not executed. |
| Backend unit and integration test coverage | implemented | partial | design docs plus representative dto/service/integration tests | Some listed categories not directly inspected. |
| System functional validation | implemented | partial | system test plan; admin/system/upload/share/trash test searches | Not all scenarios have direct evidence. |
| Security validation | implemented | partial | system test plan; admin/auth/rate-limit tests | Transport and some rate-limit paths need stronger evidence. |
| Compatibility validation | partial | missing | test/deployment/desktop docs | No executed OS/network/DB-version matrix. |
| Performance and pressure validation | partial | missing | system/performance docs and benchmark script references | No benchmark run results attached. |
| Desktop documentation validation | implemented | missing | desktop validation plan and migration matrix | Validation specified but not evidenced. |
| Evidence and reporting discipline | implemented | missing | desktop validation plan evidence/reporting sections | No command/timestamp/exit-output artifact found. |

## High-risk gaps

1. `architecture-decisions`: required ADRs are absent, including PostgreSQL selection and io_uring feasibility. This weakens long-term architectural traceability.
2. `persistence-design`: the spec remains placeholder-like and lacks concrete schema, migration, index, Redis, quota, and reference-count contracts.
3. `runtime-configuration`: PostgreSQL/Redis wiring and public route exemptions are ambiguous and lack tests.
4. `observability`: request trace visibility is not implemented as an externally verifiable behavior.
5. `trash-lifecycle`: expiry cleanup policy and scheduler behavior are unclear.
6. `deployment-operations` and `validation-and-performance`: runbooks and plans exist, but execution artifacts are missing.

## Implemented but under-verified behavior

- Registration and logout HTTP paths in `identity-and-session`.
- Binary streamed downloads in `api-contract`.
- Cross-client API consistency in `client-integration`.
- File namespace listing/details/search and file move/copy edge cases.
- Upload reservation, instant upload, and failure/retry semantics.
- Share token scope negative cases.
- Last-administrator protection across delete/disable/lock/demote operations.
- Desktop UI layout/state/terminology constraints.

## Spec clarification candidates

- Where durable ADRs should live and what filename/layout they must use.
- Whether docs-only requirements count as implemented when runtime behavior is not involved.
- Canonical PostgreSQL/Redis configuration source and startup wiring expectations.
- Exact public route exemption list and whether it must be represented in code as a single contract.
- Request trace visibility: header, log field, response body, generated ID, or propagation only.
- Trash retention policy and cleanup trigger.
- Whether validation/performance requirements require saved execution artifacts or only executable scripts/plans.

## Recommended follow-up changes

### P0 — Restore architectural and persistence source-of-truth

Proposed change: `document-architecture-and-persistence-contracts`

Scope:

- Add durable ADRs for PostgreSQL selection, download streaming API, and io_uring feasibility/no-go.
- Replace the placeholder-like `persistence-design` spec with schema, migration, index, Redis, quota, and reference-count contracts.
- Link persistence-design back to the ADRs.

Rationale: several other capabilities depend on these decisions, and current gaps are documentation-governance risks rather than isolated missing tests.

### P1 — Harden runtime configuration and traceability

Proposed change: `harden-runtime-config-and-tracing`

Scope:

- Make PostgreSQL/Redis wiring explicit and testable.
- Add startup tests for secure config, background task registration, and public-route exemptions.
- Implement or clarify request trace visibility across headers/logs/responses.

Rationale: misconfiguration and missing traceability affect operations, debugging, and security posture.

### P1 — Close high-value file lifecycle gaps

Proposed change: `complete-file-lifecycle-edge-coverage`

Scope:

- Add backend and integration tests for namespace listing/details/search, file move/copy edge cases, upload reservation/instant/resume/failure semantics, and trash expiry cleanup.
- Decide whether missing trash expiry behavior is implementation work or spec scope reduction.

Rationale: these are core product behaviors with user-visible correctness and data-safety impact.

### P2 — Strengthen sharing and admin safety tests

Proposed change: `strengthen-share-admin-safety-tests`

Scope:

- Add share token scope negative tests.
- Expand last-administrator protection tests to delete, disable, lock, and demote paths.
- Add direct HTTP-path verification for registration/logout if not covered elsewhere.

Rationale: these gaps affect access control and account safety but are narrower than the P1 infrastructure/lifecycle work.

### P2 — Capture operational and validation evidence

Proposed change: `capture-operations-validation-evidence`

Scope:

- Execute and archive evidence for deployment operations, compatibility validation, backup/restore, upgrade/rollback, performance/pressure tests, and desktop documentation validation.
- Define where evidence artifacts live and how they are refreshed.

Rationale: many operational requirements are documented but not demonstrated.

## Audit completion notes

- Every main capability under `openspec/specs/` is represented.
- Every audited requirement received an implementation and verification status.
- Evidence references were attached to every conclusion except broad search-based absence notes.
- No runtime product behavior was changed by this audit.
