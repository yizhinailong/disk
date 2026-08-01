# Backend Low-risk Cleanup Specification

## Purpose

This capability defines low-risk backend cleanup requirements for service composition, controller helper extraction, rate-limit implementation reuse, and preservation of authentication and route behavior.

## Requirements

### Requirement: Service composition boundary

The backend SHALL centralize construction of application services used by controllers behind an application-level composition boundary while preserving existing service lifetimes and startup order.

#### Scenario: Controllers obtain composed services

- **WHEN** a backend controller needs an upload, file-query, file-mutation, folder, share, or cleanup service
- **THEN** the controller SHALL obtain the existing service instance from the application composition boundary instead of directly constructing that service dependency graph

#### Scenario: Service construction side effects are preserved

- **WHEN** services are moved behind the composition boundary
- **THEN** lifecycle-sensitive side effects such as upload-task cache maintenance SHALL occur no more often than they did before the cleanup

#### Scenario: Infrastructure dependencies remain explicit

- **WHEN** composed services need DB, Redis, storage, configuration, or JWT-secret dependencies
- **THEN** those dependencies SHALL be supplied through the composition boundary without changing the externally visible behavior of existing services

#### Scenario: Composition state follows runtime access

- **WHEN** DB, Redis, and upload-staging dependencies are consumed only while the application composition boundary constructs its services
- **THEN** the boundary SHALL pass those dependencies directly into the constructed services without retaining duplicate member state, while the Blob store SHALL remain retained because controllers access it through the boundary after initialization

### Requirement: Controller helper extraction

The backend SHALL provide small shared controller helpers for mechanical request-handling patterns without changing response envelopes, validation behavior, or endpoint-specific logging.

#### Scenario: Authenticated user id is read consistently

- **WHEN** a JWT-protected controller action needs the authenticated user id
- **THEN** it SHALL read the id through a shared helper that obtains the value populated by `JwtAuthFilter`

#### Scenario: Response envelope is preserved

- **WHEN** a controller action returns a successful or failed service result through a helper
- **THEN** the HTTP response body SHALL continue to use the existing `Response::Success` or `Response::Error` envelope shape

#### Scenario: Validation remains visible

- **WHEN** a controller action parses path, query, or body input
- **THEN** endpoint-specific validation logic and validation error mapping SHALL remain explicit enough to preserve existing behavior and useful diagnostics

### Requirement: Rate-limit implementation reuse

The backend SHALL extract shared fixed-window Redis rate-limit mechanics and rate-limit response-header construction while preserving each endpoint type's current rate-limit policy.

#### Scenario: Fixed-window Redis checks are shared

- **WHEN** an upload, download, folder, register, or share-public rate-limit filter performs a fixed-window counter check
- **THEN** the filter SHALL use shared mechanics for Redis increment-with-expiry handling where practical

#### Scenario: User rate-limit filters retain no duplicate window helpers

- **WHEN** whole-repository source and compiled-object audits confirm that the private `GetCurrentWindow` and `GetResetTime` members in the API, upload, download, folder, admin, and register filters have no caller because every implementation uses `GetFixedWindowStart` and `GetFixedWindowReset`
- **THEN** all twelve dead members and their header-only `<chrono>` dependencies SHALL be removed while shared window calculations, configured or default durations, keys, Redis TTL, headers, retry timing, logging, fail-open behavior, and route policy SHALL remain unchanged

#### Scenario: Rate-limit headers are shared

- **WHEN** a request exceeds a fixed-window rate limit
- **THEN** the response SHALL include the existing `X-RateLimit-Limit`, `X-RateLimit-Remaining`, `X-RateLimit-Reset`, and `Retry-After` headers through shared header construction

#### Scenario: Existing rate-limit policy is preserved

- **WHEN** shared rate-limit mechanics are introduced
- **THEN** each filter SHALL preserve its existing route scope, key semantics, configured or default limit, and Redis failure behavior unless another approved change explicitly modifies those policies

### Requirement: Authentication and route behavior preservation

The cleanup SHALL preserve existing authentication boundaries, route-level filter attachments, public endpoint access, and share-token behavior unless a separate approved policy change modifies them.

#### Scenario: JWT-protected endpoints remain protected

- **WHEN** a route was JWT-protected before the cleanup
- **THEN** the route SHALL remain JWT-protected after the cleanup

#### Scenario: Public endpoints remain public

- **WHEN** a route was intentionally public before the cleanup
- **THEN** the route SHALL remain reachable without JWT after the cleanup

#### Scenario: Share-token endpoints keep share authentication

- **WHEN** a route requires share-token authentication before the cleanup
- **THEN** the route SHALL continue to require share-token authentication after the cleanup

#### Scenario: Filter execution dependencies are respected

- **WHEN** a rate-limit filter depends on `user_id` populated by JWT authentication
- **THEN** the cleanup SHALL preserve an execution order in which JWT authentication can populate `user_id` before that rate-limit filter evaluates the request

### Requirement: Filter helper surfaces follow production use

Backend filters SHALL expose only helper capabilities consumed across production translation units while keeping internal parsing and generation logic directly covered through active filter behavior.

#### Scenario: Request-trace generation helpers are internalized

- **WHEN** whole-repository and compiled-object audits confirm that `RequestTraceFilter::GenerateRequestId` and `IsValidRequestId` have no external production consumer while `main.cpp` and the global filter both depend on `ResolveRequestId`
- **THEN** generation and validation SHALL become implementation-private helpers while `ResolveRequestId`, safe upstream-ID acceptance, invalid-ID UUID fallback, existing-attribute preservation, response correlation, and process-drain request admission retain direct coverage

#### Scenario: JWT public-path classification is internalized

- **WHEN** whole-repository and compiled-object audits confirm that `JwtAuthFilter::IsPublicPath` is consumed in production only by the filter implementation while direct external calls belong only to unit tests
- **THEN** public-path classification SHALL become implementation-private and auth, health, metrics, public-share, and protected near-miss behavior SHALL be covered through `JwtAuthFilter::doFilter` without changing global-filter ownership or route-level protection

### Requirement: Repository surfaces follow production use

Backend repository classes SHALL expose only persistence primitives used by production flows, while persisted compatibility contracts remain governed by their separate migration-retirement gates.

#### Scenario: Unused upload-task wrappers are removed

- **WHEN** a whole-repository call-site audit confirms that an upload-task lookup, transition, coverage query, or non-transactional delete wrapper has no production caller
- **THEN** the declaration, implementation, and obsolete source-contract expectation SHALL be removed together

#### Scenario: Active upload lifecycle primitives remain explicit

- **WHEN** completion, cancellation, expiration, or chunk persistence needs a PostgreSQL primitive
- **THEN** the repository SHALL retain the owner/version guarded transition or transaction-client overload used by that production flow

#### Scenario: Unused default-client lease renewal is removed

- **WHEN** whole-repository call-site and built-object audits confirm that the five-argument `UploadTaskRepository::RenewFinalizeLease` only forwards to the explicit-client overload and has no production, integration, tool, client, or migration consumer
- **THEN** the declaration and forwarding implementation SHALL be removed while the six-argument transaction-client overload, owner/version/expiry predicate, PostgreSQL time, and returned next version remain unchanged

#### Scenario: Finalize claims omit unread diagnostic state

- **WHEN** `finalize_attempts` is a durable PostgreSQL diagnostic counter incremented by the claim CAS but no production flow reads it from `FinalizeClaimResult`
- **THEN** the result field, query projections, and row assignments SHALL be removed while the database column, atomic increment, diagnostics, disposition, state version, completed replay ID, and claim behavior remain unchanged

#### Scenario: Unused descendant file-path update primitive is removed

- **WHEN** a whole-repository call-site audit confirms that `FileRepository::UpdateDescendantFilePathsForFolderMove` and its dedicated SQL have no production or behavioral-test caller
- **THEN** the declaration, implementation, obsolete positive source-contract expectation, and dedicated SQL SHALL be removed while folder rename and move flows continue to update each descendant file through transaction-bound `UpdateFilePath` calls

#### Scenario: Unused non-locking owned-file lookup is removed

- **WHEN** whole-repository call-site and built-object audits confirm that `FileRepository::FindOwnedFile` and its dedicated SQL have no production, integration, tool, client, or migration consumer and are retained only by positive source-contract tests
- **THEN** the declaration, implementation, dedicated SQL, and obsolete positive expectations SHALL be removed while mutating flows continue to use transaction-bound `FindOwnedFileForUpdate` and read-only API flows keep their existing user-scoped ORM queries

#### Scenario: Unused single-folder delete plan path is removed

- **WHEN** whole-repository call-site and built-object relocation audits confirm that `file::utils::FetchFolderDeletePlan` only forwards to `FolderRepository::FetchFolderDeletePlan` and that repository operation has no other production, test, tool, client, or migration consumer
- **THEN** both layers SHALL be removed while `FetchFolderSubtree` and all transaction-bound or standalone `FetchBatchFolderDeletePlans` flows retain their ownership predicates, connection scope, snapshots, and failure behavior

#### Scenario: Batch folder delete plans use the repository directly

- **WHEN** whole-repository call-site and source audits confirm that `file::utils::FetchBatchFolderDeletePlans` only default-constructs `FolderRepository` and forwards the same client, folder IDs, and user ID while the services already own or create that repository
- **THEN** the utility declaration and implementation SHALL be removed, and the two FileMutation flows plus the transaction-bound Trash flow SHALL call `FolderRepository::FetchBatchFolderDeletePlans` directly without changing connection scope, ownership predicates, plan stabilization, snapshots, or public behavior

#### Scenario: Folder location uses the repository directly

- **WHEN** whole-repository call-site and built-object audits confirm that `file::utils::ResolveFolderLocation` only forwards the same client, folder ID, user ID, and log context to `FolderRepository::ResolveOwnedFolderLocation`
- **THEN** the utility declaration and implementation SHALL be removed, all UploadLifecycle and FileMutation callers SHALL use their existing repository object directly, and folder lookup SQL, user scope, transaction ownership, logging, errors, paths, and public behavior SHALL remain unchanged

#### Scenario: Trash insertion uses its persistence helper directly

- **WHEN** whole-repository call-site and built-object audits confirm that public `TrashService::CreateTrashRecords` only forwards the same client, items, user ID, and log context to `file::utils::InsertTrashRecords` and is otherwise called only by `MoveToTrash`
- **THEN** the service declaration and implementation SHALL be removed, `MoveToTrash` SHALL call the persistence helper directly inside the same transaction, and insert SQL, logging, rollback, errors, counts, and public behavior SHALL remain unchanged

#### Scenario: File repository retains no unused default client

- **WHEN** every `FileRepository` operation already receives the standalone client or active transaction explicitly and the constructor-injected client is never read
- **THEN** the repository SHALL be default-constructible and stateless, its unused constructor and client field SHALL be removed, and every production call SHALL continue to supply its operation-scoped client

#### Scenario: Folder repository retains no unused default client

- **WHEN** every `FolderRepository` operation already receives the standalone client or active transaction explicitly and the constructor-injected client is never read
- **THEN** the repository SHALL be default-constructible and stateless, its unused constructor and client field SHALL be removed, and every production call SHALL continue to supply its operation-scoped client

#### Scenario: Dead-letter replay remains audit-transaction owned

- **WHEN** the only production dead-letter replay flow resets a job and records its administrator audit in one transaction
- **THEN** `StorageJobAdminService` SHALL retain that atomic flow and `StorageJobRepository` SHALL NOT expose a standalone `ReplayDeadLetter` operation that can bypass the audit write

#### Scenario: Interface cleanup does not retire persisted compatibility

- **WHEN** unused C++ repository methods are removed before migration-retirement approval
- **THEN** persisted upload status values, local-staging descriptors, legacy path fallback, and nullable migration fields SHALL remain available until their dedicated retirement contract admits removal

#### Scenario: Cleanup records omit duplicate legacy path state

- **WHEN** cancellation and expiration flows consume only the unified staging descriptor produced by `COALESCE(staging_prefix, temp_path)` and never read the separately projected legacy path
- **THEN** `UploadTaskCleanupRecord::temp_path`, its independent query projections, and row assignment SHALL be removed while the database column, legacy fallback expression, backend, unified prefix, quota release, cleanup enqueue, and migration compatibility remain unchanged

#### Scenario: Test-only upload-state rules are removed

- **WHEN** a whole-repository call-site audit confirms that generic transition, in-memory lease, and legacy complete/cancel/expire guards are called only by `UploadStateMachine` unit tests while production uses request decisions and PostgreSQL conditional mutations
- **THEN** those parallel pure-function rules and their dedicated tests SHALL be removed while active status parsing, terminal classification, finalize/cancel decisions, and database owner/version fencing retain direct coverage

#### Scenario: Test-only integer terminal wrapper is removed

- **WHEN** whole-repository and compiled-object audits confirm that `IsTerminalStatus(int)` is referenced only by its dedicated unit-test assertion while recovery management consumes `IsTerminalStatus(UploadTaskStatus)`
- **THEN** the integer overload SHALL be removed while unknown storage-value rejection, typed terminal classification, recovery-plan admission, and PostgreSQL-guarded lifecycle decisions retain direct coverage

#### Scenario: Test-only chunk-coverage rule is removed

- **WHEN** a whole-repository call-site audit confirms that `ChunkCoverage` and `IsCompleteCoverage` are used only by dedicated pure-function unit tests while production completion admission validates chunk count and maximum index inside `UploadTaskRepository::ClaimFinalizeLease`
- **THEN** the unused snapshot type, parallel rule, and dedicated tests SHALL be removed while source contracts and real PostgreSQL tests retain direct coverage of the atomic finalize-claim predicate

### Requirement: Shared service surfaces follow production use

Backend shared services SHALL expose only command capabilities used by production flows, while active command behavior and dependency-failure policy remain unchanged.

#### Scenario: Unused service validation exception is removed

- **WHEN** a whole-repository audit confirms that `ServiceValidationException` is only defined in `FileServiceUtils.hpp` and has no construction, throw, catch, production, integration, tool, or client consumer
- **THEN** the unused exception type SHALL be removed while file mutation validation continues to return `ErrorInfo` through `Result<T>`/`std::unexpected` and preserves its transaction rollback and public error behavior

#### Scenario: Test-only DTO scalar helpers are removed

- **WHEN** a whole-repository call-site audit confirms that `DtoBase::RequireBool` and `DtoBase::OptionalInt` are called only by `DtoBase_test.cpp` while every production request DTO uses the remaining typed helpers
- **THEN** the two unused helpers and their dedicated assertions SHALL be removed while source contracts reject their reintroduction and all active JSON, path, query, and ID-array parsers retain their existing `Result<T>` errors without logging

#### Scenario: Shared drive-item name validation is reused

- **WHEN** file upload, file rename, folder creation, and folder rename duplicate the same forbidden-character loop while `NameValidation` already provides the identical cross-platform predicate
- **THEN** all four DTO validators SHALL reuse that shared predicate and source contracts SHALL lock the production call sites while validation order, public errors, reserved-name rules, hidden-name rules, and UTF-8 handling remain unchanged

#### Scenario: File extension extraction is shared by active-file writers

- **WHEN** upload finalization and file rename use identical last-dot extension extraction while both implementations already depend on `FileServiceUtils`
- **THEN** their three active metadata write sites SHALL use one shared `ExtractFileExtension` helper without changing suffix bytes, empty-extension cases, validation order, names, paths, transactions, responses, or the restore-specific trash conflict-name parser

#### Scenario: Folder snapshot timestamp conversion stays implementation-local

- **WHEN** a whole-repository call-site, history, and object-symbol audit confirms that `DateToJson` is used only by `BuildFolderSnapshot` in `FileServiceUtils.cpp` and was implementation-local before the service split
- **THEN** the helper SHALL have internal linkage and SHALL NOT appear in `FileServiceUtils.hpp`, while all root, folder, and file snapshot timestamps continue to use `trantor::Date::toDbStringLocal()` with unchanged JSON fields and values

#### Scenario: Unused DTO serialization overloads are removed

- **WHEN** a whole-repository call-site and built-object audit confirms that the `DtoBase::SetField` overloads for `std::string_view` and `const char*` plus the `DtoBase::SetArray` overloads for `std::vector<uint64_t>` and `std::vector<std::string>` have no production consumer or object-code instantiation
- **THEN** the four unused overloads and their now-unused include SHALL be removed while source contracts reject their reintroduction and every active scalar, nested-object, optional, nullable, object-array, and `uint32_t`-array response keeps its existing JSON shape

#### Scenario: Test-only share-status formatter is removed

- **WHEN** a whole-repository call-site audit confirms that `ShareStatusToString` is called only by two duplicate sets of enum-formatting assertions while production uses `ShareStatus` for database state decisions and response DTOs serialize their existing status strings directly
- **THEN** the unused formatter and its six dedicated assertions SHALL be removed while a source contract rejects its reintroduction and active enum values, list-status validation, database state handling, and response JSON status fields retain their existing coverage

#### Scenario: Unused share-token allowlist key is removed

- **WHEN** a whole-repository audit confirms that the `share_token:{share_code}:{token_hash}` prefix and builder are used only by key-format unit tests while share issuance returns a self-contained JWT and production verification uses the optional blacklist plus live database state
- **THEN** the unused allowlist namespace, builder, and dedicated tests SHALL be removed while refresh-token, access/share blacklist, file-list cache, and rate-limit key builders retain their existing formats and behavior

#### Scenario: Unused share-token revocation writer is removed

- **WHEN** a whole-repository call-site audit confirms that `TokenService::RevokeShareToken` has no production caller and is used only to seed one filter test
- **THEN** the unused writer SHALL be removed and that test SHALL seed the exact Redis blacklist hash directly while blacklist reads, positive revocation caching, Redis failure handling, and live database share-status checks retain their existing behavior

#### Scenario: Redundant access-revocation cache probe is removed

- **WHEN** a whole-repository audit confirms that `TokenService::IsRevocationCacheEntryRevokedForTest` is used only by two assertions that duplicate adjacent zero/one cache-size checks
- **THEN** the redundant production-header test probe and duplicate assertions SHALL be removed while cache insertion/removal, expiry, Redis-backed revocation checks, and access-token rejection retain their existing coverage

#### Scenario: Test-only combined file hash helper is removed

- **WHEN** a whole-repository audit confirms that `FileHashPair` and `FileHashUtil::HashFileMd5AndSha256` are used only by dedicated unit tests while Local and S3 assembly compute whole-file MD5 and SHA-256 during their own bounded streaming pass
- **THEN** the unused result type, helper, and dedicated tests SHALL be removed while active string/file hash helpers, incremental MD5 primitives, and both storage assembly integrity paths retain direct coverage

#### Scenario: Test-only MD5 verification wrapper is removed

- **WHEN** a whole-repository audit confirms that `FileHashUtil::VerifyHash` only wraps `HashMd5(data) == expected_md5` and is called only by dedicated unit tests while `UploadService` directly computes and compares each production chunk hash
- **THEN** the unused wrapper and dedicated tests SHALL be removed while `HashMd5`, the single-hash upload path, mismatch errors, and storage handoff of the computed hash retain direct coverage

#### Scenario: Unused generic Result response adapters are removed

- **WHEN** a whole-repository audit confirms that both `Response::FromResult` overloads have no production caller and only the void overload is exercised by dedicated unit tests while controllers explicitly select success, pagination, or error responses at the HTTP boundary
- **THEN** the unused adapters and dedicated tests SHALL be removed while active success, pagination, error, and startup-failure response builders retain their existing envelope and status behavior

#### Scenario: Single-use custom-error response alias is removed

- **WHEN** a whole-repository and compiled-object audit confirms that `Response::Fail` only forwards to `Response::Error(ErrorInfo(code, message))` and has exactly one production caller in the process-drain rejection path
- **THEN** that caller SHALL use the canonical `ErrorInfo` path and the alias SHALL be removed while the custom message, uniform error envelope, HTTP 503 override, `Retry-After` header, and process-drain behavior retain direct regression contracts

#### Scenario: Single-use batch-input validator is removed

- **WHEN** a whole-repository and compiled-object audit confirms that `BatchUtils::ValidateBatchInput` has one string-vector instantiation whose sole caller passes `size_t::max()` so the upper-bound predicate is tautological
- **THEN** `ShareService::Cancel` SHALL retain its empty-input success branch directly and the generic validator SHALL be removed while active chunking, parameterized placeholder construction, DTO rejection of empty HTTP arrays, and non-empty batch-cancel behavior retain direct coverage

#### Scenario: Unused Redis batch-command surface is removed

- **WHEN** a whole-repository call-site audit confirms that `RedisService::MSet`, `MGet`, `MDelete`, the `KeyValue` input type, and their command builders have no production or behavioral-test caller
- **THEN** those declarations, implementations, and placeholder tests SHALL be removed together while active single-key, counter, CAS, and fixed-window operations retain their existing behavior

#### Scenario: Unused standalone Redis mutations are removed

- **WHEN** a whole-repository call-site audit confirms that `RedisService::Expire`, `RedisService::IncrBy`, and the `TtlType` enum have no production caller and are exercised only by direct Redis service tests
- **THEN** those declarations, implementations, and dedicated expectations SHALL be removed while `Set` retains atomic write-with-TTL behavior, `Incr` retains cache-generation increments, `CompareAndSwap` retains refresh-token rotation, and `IncrWithExpire` retains atomic fixed-window rate limiting

#### Scenario: Unused unchecked content-reference wrapper is removed

- **WHEN** a whole-repository call-site audit confirms that `ContentService::IncrementRefCounts` has no production caller and only converts `IncrementRefCountsChecked` failures into an empty set
- **THEN** the unchecked wrapper and its dead warning event SHALL be removed while copy transactions continue to call the checked operation directly and propagate failures for rollback

#### Scenario: Unused generic operation-log writer is removed

- **WHEN** a whole-repository call-site audit confirms that `OperationLogService::Log` and its entry, enum, normalization, and conversion helpers have no production caller while authentication, sharing, administrator, storage-job, and recovery domains own active audit writes
- **THEN** the generic write subgraph SHALL be removed while `OperationLogService::GetList` retains the user-visible query contract and each active domain write retains its existing transaction and failure policy

#### Scenario: Unused Worker startup probe is removed

- **WHEN** a whole-repository call-site audit confirms that `StorageWorkerRuntime::IsStarted` has no production or test caller
- **THEN** the public probe SHALL be removed while the internal atomic startup guard, idempotent `Start`, drain admission, and in-flight drain observation remain unchanged

#### Scenario: Unused runtime admission probes are removed

- **WHEN** a whole-repository call-site audit confirms that `ScheduledTasks::IsAccepting` has no caller and `StorageWorkerRuntime::IsAccepting` is only used by redundant test assertions while `PollOnce` already proves the admission behavior
- **THEN** both public probes and the redundant assertions SHALL be removed while internal admission state, Worker polling, scheduler seeding, drain or stop behavior, and in-flight drain observation remain unchanged

#### Scenario: Worker runtime retains no unused identity input

- **WHEN** a whole-repository data-flow audit confirms that `StorageWorkerRuntime` only validates and discards its instance ID while `StorageJobWorker` independently validates, stores, and uses the same configured identity for lease ownership
- **THEN** the runtime constructor SHALL accept only its execution callback and polling options while Worker lease identity, scheduler deduplication, structured-log instance registration, polling, drain, and shutdown behavior remain unchanged

#### Scenario: Scheduled tasks retain no unused identity input

- **WHEN** a whole-repository data-flow audit confirms that `ScheduledTasks::Initialize` only validates and discards its instance ID while `StorageJobWorker` owns lease identity and PostgreSQL dedupe keys coordinate periodic seeding
- **THEN** scheduler initialization SHALL accept only its database dependency while Worker lease ownership, structured-log instance registration, six-job seed plans, deduplication, role admission, drain, and shutdown behavior remain unchanged

#### Scenario: Unused share timestamp helper is removed

- **WHEN** a whole-repository call-site audit confirms that `ShareService::UpdateTimestamp` has no production or test caller
- **THEN** the private helper SHALL be removed while share update and cancellation continue to own their timestamp writes and access or download flows retain their existing counter semantics

#### Scenario: Unused credential getters are removed

- **WHEN** a whole-repository call-site audit confirms that `ConfigMgr::GetDatabasePassword` and `ConfigMgr::GetRedisPassword` have no production or test caller
- **THEN** those getters SHALL be removed while startup environment overrides continue to inject credentials into Drogon client configuration and secure-mode validation retains its existing fail-closed checks

#### Scenario: Runtime config file loading stays internal

- **WHEN** a whole-repository call-site and object-relocation audit confirms that `RuntimeConfig::LoadFile` is only called by `LoadFromEnvironment` in the same implementation unit
- **THEN** the public member SHALL be replaced by an implementation-local file loader while path selection, JSON parsing, database routing validation, environment overrides, and startup failure behavior remain unchanged

#### Scenario: Runtime config exposes one complete pipeline

- **WHEN** call-site and object-relocation audits confirm that `RuntimeConfig::ValidateDatabaseRouting` and `RuntimeConfig::ApplyEnvironmentOverrides` have no production consumer outside `LoadFromEnvironment` and are otherwise called only by unit tests
- **THEN** both stages SHALL become implementation-local, tests SHALL exercise them through temporary files and the composite loader, and `RuntimeConfig` SHALL expose only `LoadFromEnvironment` without changing validation, override, or failure behavior

#### Scenario: Unused quota reconciliation facade is removed

- **WHEN** whole-repository call-site and object-relocation audits confirm that both `QuotaService::GetReconciliation` overloads only reference each other, their result type is only exercised by a field-assignment test, and `StorageReconciliationService` owns the durable users-scope reconciliation flow
- **THEN** both overloads, their dedicated result type, SQL, log event, and dead test SHALL be removed while active quota mutations and durable quota findings retain their existing transaction, context, and failure behavior

#### Scenario: Quota mutations require an explicit database client

- **WHEN** a whole-repository call-site and object-relocation audit confirms that the standalone `QuotaService::ReserveStorage` and `ReserveUploadStorage` overloads have no caller while every active mutation supplies a standalone or transaction client
- **THEN** the two default-client overloads and stored client SHALL be removed, `QuotaService` SHALL be empty and default-constructible, and all active quota SQL, checked results, logging context, and construction event behavior SHALL remain unchanged

#### Scenario: Upload quota reservation uses the generic mutation directly

- **WHEN** whole-repository call-site and object-relocation audits confirm that the explicit-client `QuotaService::ReserveUploadStorage` only forwards the same client, user ID, byte count, and log context to `ReserveStorage` and is otherwise called only by upload initialization
- **THEN** the forwarding declaration and implementation SHALL be removed, upload initialization SHALL call `ReserveStorage` directly inside the same transaction, and quota SQL, zero-byte behavior, logging, checked errors, rollback, upload state, and public responses SHALL remain unchanged

#### Scenario: Content operations require an explicit database client

- **WHEN** the only standalone `ContentService` operation is an MD5 lookup whose active callers already own a database client while every content mutation receives a standalone or transaction client explicitly
- **THEN** the standalone lookup overload and stored client SHALL be removed, `ContentService` SHALL be empty and default-constructible, every production operation SHALL pass its client explicitly, and reference-gate and Blob-GC repository calls SHALL use that same operation client without changing SQL, locking, enqueue, logging, or response behavior

#### Scenario: System information retains no unused inputs

- **WHEN** whole-repository source and object audits confirm that `SystemService::GetInfo` never reads its user-id argument and the constructor-injected Redis client is only stored while connection-pool reporting reads configuration
- **THEN** the user-id argument and Redis client dependency SHALL be removed, the controller SHALL retain its authenticated `user_id` attribute gate and `TokenMissing` response before calling the service with context only, and system information, DB queries, configured pool sizes, logging, route authentication, and response behavior SHALL remain unchanged

#### Scenario: Operation-log reads omit redundant user state

- **WHEN** the current-user operation-log query already scopes both count and page reads with `WHERE user_id = $1` while the response never emits the stored user ID
- **THEN** `OperationLogItem::user_id`, its SELECT projection, and row assignment SHALL be removed while the user predicate, database schema, pagination, audit writes, logging, errors, and JSON response remain unchanged

#### Scenario: Unused token-lifetime config facade is removed

- **WHEN** a whole-repository read/write audit confirms that the `ConfigMgr` access/refresh token lifetime getters and fixed members have no production or test caller and no JSON or environment loading path
- **THEN** that parallel config facade SHALL be removed while `TokenService` remains the single owner of the access/refresh signing, response, and revocation-cache lifetime contract

#### Scenario: Unused Auth CPU metrics wrappers are removed

- **WHEN** a whole-repository call-site audit confirms that `detail::StartAuthCpuPoolMetricsTimer` and `detail::GetAuthCpuPoolActiveTaskCount` have no production or test caller
- **THEN** those wrappers SHALL be removed while the Auth CPU work-loop bridge, startup-owned metrics timer, atomic counters, periodic metrics, and request execution behavior remain unchanged

#### Scenario: Manual cleanup stages stay internal

- **WHEN** a whole-repository call-site audit confirms that `CleanupService::CleanupExpiredTrash` and `CleanupService::CleanupExpiredUploadTasks` are called only by `RunExpiredCleanupOnce` in the same class
- **THEN** both stage methods SHALL become private while the public composite entry point, aggregate result, trash and upload ordering, error propagation, logging context, and administrator route behavior remain unchanged

#### Scenario: Internal share-code lookup stays private

- **WHEN** a whole-repository call-site audit confirms that `ShareService::FindShareByCode` is called only by share access and failed-access handling inside `ShareService`
- **THEN** the lookup helper SHALL become private while controller-facing share commands, the public download-completion hook, lookup errors, access throttling, audit writes, and request context propagation remain unchanged

#### Scenario: Periodic seeding execution stays internal

- **WHEN** a whole-repository call-site audit confirms that `ScheduledTasks::SeedOnce` is called only by the private event-loop trigger
- **THEN** the seeding stage SHALL become private while initialization, registration, shutdown, drain observation, immediate and periodic triggers, durable deduplication, fixed error handling, and role ownership remain unchanged

#### Scenario: Per-upload expiration execution stays internal

- **WHEN** a whole-repository call-site audit confirms that `UploadLifecycleService::ExpireInProgressUpload` is called only by initialization-time stale-task handling and the bounded batch expiration flow inside the same class
- **THEN** the per-upload stage SHALL become private while external cleanup and Worker callers retain the public bounded batch entry point and PostgreSQL conditional transition, quota release, durable cleanup enqueue, chunk deletion, cache invalidation, logging context, and error propagation remain unchanged

#### Scenario: Unused trash ID forwarding overloads are removed

- **WHEN** a whole-repository call-site audit confirms that the private `TrashService` overloads which accept only a `trash_id` for file/folder restore or permanent delete are never called because the active batch entries prefetch `TrashLifecycleRecord` values
- **THEN** those four standalone-query forwarding overloads SHALL be removed while the record-based restore and permanent-delete paths retain their locked transactions, batch results, content references, quota accounting, cache invalidation, and logging context

#### Scenario: Unused trash permanent-delete wrappers are removed

- **WHEN** a whole-repository call-site audit confirms that the record-based private `TrashService::DeleteFile` and `TrashService::DeleteFolder` wrappers have no caller because `Delete` validates each prefetched item and invokes `PermanentlyDeleteTrashItems` directly
- **THEN** those two unreachable wrappers SHALL be removed while the active batch path retains per-item validation and results, atomic content-reference and quota updates, Blob GC enqueueing, error messages, and logging context
