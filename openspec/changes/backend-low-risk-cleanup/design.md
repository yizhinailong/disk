## Context

This change covers Phase 1 low-risk structure cleanup from `docs/TODO.md`. The backend currently keeps startup infrastructure in `src/main.cpp` while several controllers still construct their own service dependency graphs:

- `FileController` constructs upload, file-query, and file-mutation services and reads storage directly.
- `FolderController` constructs its folder service directly.
- `ShareController` constructs its share service with DB, Redis, and JWT-secret dependencies and reads storage directly.
- Rate-limit filters duplicate the same fixed-window Redis pattern and rate-limit response headers.

The cleanup must preserve externally visible behavior. It should prepare the codebase for later domain extraction without changing upload, download, share, quota, trash, content-ref-count, authentication, or public endpoint semantics.

## Goals / Non-Goals

**Goals:**

- Centralize backend service construction behind a small application-level composition boundary.
- Let controllers obtain existing service instances instead of constructing dependency graphs directly.
- Add small controller helpers for repeated authenticated-user and result/response patterns where doing so remains readable.
- Extract reusable fixed-window Redis rate-limit mechanics and response-header construction.
- Preserve public API behavior, route declarations, response envelope shape, authentication requirements, rate-limit policy, Redis failure behavior, and service lifetimes.
- Keep the implementation incremental enough to review in small batches.

**Non-Goals:**

- Do not introduce a large dependency-injection framework.
- Do not change JWT from route-level to global enforcement, or vice versa.
- Do not change which endpoints are public, JWT-protected, share-token-protected, upload-limited, download-limited, or folder-limited.
- Do not change Redis fail-open/fail-closed policy.
- Do not change upload/content/quota/trash business rules.
- Do not split staging/blob storage or introduce object storage support.
- Do not hide validation details behind a generic controller framework.

## Decisions

### Decision 1: Use a small application service context, not a DI framework

Introduce a narrow application-level context or registry that owns or exposes the existing service instances needed by controllers. The context should be initialized after infrastructure dependencies such as configuration, DB client access, Redis client access, and storage setup are available.

Rationale: the current problem is scattered construction, not a need for runtime service discovery. A small context keeps object lifetimes visible and avoids adding framework complexity.

Alternatives considered:

- Keep controller-local construction: rejected because it preserves duplication and makes later domain extraction harder.
- Add a full DI container: rejected because it is disproportionate for this cleanup and would increase review risk.

### Decision 2: Preserve service lifetime and startup semantics

Service construction must remain compatible with the current startup order. Particular care is required for services that start background or maintenance work during construction, such as upload-task cache maintenance.

Rationale: moving construction is only low-risk if the number of constructed service instances and side effects stay stable.

Alternatives considered:

- Reconstruct services per request or per controller method: rejected because it could alter performance and background-maintenance behavior.
- Lazily construct all services without guardrails: rejected unless lifecycle-sensitive services are proven safe.

### Decision 3: Keep controller helpers small and explicit

Start with helpers for reading authenticated `user_id` from request attributes. Add result-to-response helpers only where they preserve existing logs and do not obscure request validation.

Rationale: controllers currently contain useful endpoint-specific logging and validation context. A small helper removes mechanical repetition without creating a generic controller pipeline.

Alternatives considered:

- Create a full generic request handler pipeline: rejected because it would hide validation and logging details.
- Avoid helpers entirely: rejected because repeated authenticated-user access is mechanical and error-prone.

### Decision 4: Extract rate-limit mechanics separately from rate-limit policy

Extract shared fixed-window Redis increment/expiry handling and shared header construction, while preserving each endpoint type's current key shape, configured limit, route attachment, and Redis failure behavior unless explicitly changed by a later proposal.

Rationale: implementation reuse is low-risk; policy consolidation is not. Upload, download, folder, register, and share-public limits may currently differ in configurability, key format, and route scope.

Alternatives considered:

- Normalize all limits and failure behavior now: rejected because that is a policy change.
- Leave all rate-limit code duplicated: rejected because common Redis/header behavior is already repeated and can be safely factored after discovery.

### Decision 5: Do filter discovery before filter declaration changes

Before changing route-level or global filter declarations, document current execution order and verify that protected endpoints execute JWT and rate-limit filters exactly as expected.

Rationale: rate-limit filters consume `user_id` set by `JwtAuthFilter`. Reordering filters could silently bypass limits.

Alternatives considered:

- Move directly to global JWT with exemptions: rejected because it changes a security boundary and requires separate policy approval.

## Risks / Trade-offs

- Service construction side effects change → Mitigation: migrate one controller at a time, verify service instance counts and background-maintenance behavior, and keep construction order explicit.
- Controller helper abstraction hides endpoint-specific validation or logs → Mitigation: limit helpers to mechanical patterns and keep endpoint-specific parse and logging code local.
- Rate-limit extraction accidentally changes limits, keys, headers, or fail-open behavior → Mitigation: characterize current behavior first and preserve per-filter policy inputs when extracting shared mechanics.
- Filter order changes cause rate limits to skip because `user_id` is absent → Mitigation: do not change filter declarations until execution order is documented and covered by tests.
- Public/share endpoints are misclassified during cleanup → Mitigation: treat share access, share browse/download, and share save as separate authentication shapes.

## Migration Plan

1. Add or identify characterization tests for representative controller responses and filter/rate-limit behavior.
2. Introduce controller helper(s) and migrate one simple controller path first.
3. Introduce the service composition boundary and migrate `FolderController` first because it has the simplest dependency graph.
4. Migrate `FileController` while checking upload-service construction and storage access behavior.
5. Migrate `ShareController` while preserving DB, Redis, JWT-secret, storage, JWT, and share-token behavior.
6. Extract fixed-window Redis rate-limit mechanics and rate-limit header construction without changing per-filter policy.
7. Run the existing backend test suite and any new characterization tests.

Rollback strategy: each step should be small enough to revert independently. If lifecycle or filter behavior changes unexpectedly, revert the affected controller/filter migration while keeping unrelated helper extraction only if verified safe.

## Open Questions

- Which existing test target should serve as the required characterization suite for controller response and rate-limit behavior?
- Should result-to-response mapping be limited to DTOs with `ToJson()`, or remain mostly explicit in controllers?
- Should policy-level rate-limit configuration consistency be proposed as a separate follow-up change after this cleanup?
