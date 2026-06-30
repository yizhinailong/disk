## Why

Authentication and authorization filters are currently applied both globally and on many routes, causing protected requests to run the same JWT and admin checks more than once. This creates avoidable CPU/Redis work, duplicated logs, and ambiguity about whether new endpoints should be secured through `config.json` or `ADD_METHOD_TO` route filters.

## What Changes

- Establish a single ownership model for filter application:
  - request tracing remains global;
  - authentication and admin authorization become route-owned;
  - rate-limit filters are kept either global-with-path-guard or route-owned, but no individual filter is applied through both paths.
- Remove duplicate global application of `JwtAuthFilter` and `AdminAuthFilter` once protected routes explicitly declare the required filters.
- Ensure routes that currently rely only on global authentication, such as logout, declare route-level authentication before global JWT removal.
- Preserve public endpoint behavior for registration, login, refresh, health checks, and public share access/browse/download flows.
- Preserve existing rate-limit semantics while documenting which rate-limit filters are path-scoped globals and which are route-scoped.
- Add validation coverage that catches duplicate filter registration and verifies protected/public route behavior remains intact.

## Capabilities

### New Capabilities
- `request-filter-application`: Defines the backend contract for applying global and route-level request filters without duplicate security or rate-limit execution.

### Modified Capabilities
- `identity-and-session`: Clarifies that protected bearer-token APIs remain authenticated when JWT enforcement moves from global configuration to explicit route filters.
- `runtime-configuration`: Clarifies that public-route exemptions are no longer the primary mechanism for JWT protection when authentication is route-owned.

## Impact

- Affected configuration: `config.json` global filter plugin entries.
- Affected route declarations: controller `METHOD_LIST_BEGIN` / `ADD_METHOD_TO` definitions, especially authenticated routes that currently rely on global JWT enforcement.
- Affected filters: `JwtAuthFilter`, `AdminAuthFilter`, `RequestTraceFilter`, `DownloadRateLimitFilter`, `AdminRateLimitFilter`, `FolderRateLimitFilter`, `RegisterRateLimitFilter`, `SharePublicRateLimitFilter`, `UploadRateLimitFilter`, `RateLimitFilter`, and `ShareAuthFilter` application policy.
- Affected validation: focused tests or checks for public routes, protected routes, admin routes, and absence of duplicate filter invocation/registration.
