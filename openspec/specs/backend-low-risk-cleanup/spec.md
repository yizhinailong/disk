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

#### Scenario: Unused descendant file-path update primitive is removed

- **WHEN** a whole-repository call-site audit confirms that `FileRepository::UpdateDescendantFilePathsForFolderMove` and its dedicated SQL have no production or behavioral-test caller
- **THEN** the declaration, implementation, obsolete positive source-contract expectation, and dedicated SQL SHALL be removed while folder rename and move flows continue to update each descendant file through transaction-bound `UpdateFilePath` calls

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

#### Scenario: Test-only upload-state rules are removed

- **WHEN** a whole-repository call-site audit confirms that generic transition, in-memory lease, and legacy complete/cancel/expire guards are called only by `UploadStateMachine` unit tests while production uses request decisions and PostgreSQL conditional mutations
- **THEN** those parallel pure-function rules and their dedicated tests SHALL be removed while active status parsing, terminal classification, finalize/cancel decisions, and database owner/version fencing retain direct coverage

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

#### Scenario: Unused share timestamp helper is removed

- **WHEN** a whole-repository call-site audit confirms that `ShareService::UpdateTimestamp` has no production or test caller
- **THEN** the private helper SHALL be removed while share update and cancellation continue to own their timestamp writes and access or download flows retain their existing counter semantics

#### Scenario: Unused credential getters are removed

- **WHEN** a whole-repository call-site audit confirms that `ConfigMgr::GetDatabasePassword` and `ConfigMgr::GetRedisPassword` have no production or test caller
- **THEN** those getters SHALL be removed while startup environment overrides continue to inject credentials into Drogon client configuration and secure-mode validation retains its existing fail-closed checks

#### Scenario: Runtime config file loading stays internal

- **WHEN** a whole-repository call-site and object-relocation audit confirms that `RuntimeConfig::LoadFile` is only called by `LoadFromEnvironment` in the same implementation unit
- **THEN** the public member SHALL be replaced by an implementation-local file loader while path selection, JSON parsing, database routing validation, environment overrides, and startup failure behavior remain unchanged

#### Scenario: Unused token-lifetime config facade is removed

- **WHEN** a whole-repository read/write audit confirms that the `ConfigMgr` access/refresh token lifetime getters and fixed members have no production or test caller and no JSON or environment loading path
- **THEN** that parallel config facade SHALL be removed while `TokenService` remains the single owner of the access/refresh signing, response, and revocation-cache lifetime contract

#### Scenario: Unused Auth CPU metrics wrappers are removed

- **WHEN** a whole-repository call-site audit confirms that `detail::StartAuthCpuPoolMetricsTimer` and `detail::GetAuthCpuPoolActiveTaskCount` have no production or test caller
- **THEN** those wrappers SHALL be removed while the Auth CPU work-loop bridge, startup-owned metrics timer, atomic counters, periodic metrics, and request execution behavior remain unchanged
