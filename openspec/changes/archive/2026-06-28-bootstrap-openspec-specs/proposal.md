## Why

The project already has extensive design, API, database, test, and client documentation, but the OpenSpec main specs are still empty. Converting the current stable behavior into capability-oriented OpenSpec specs will give future changes a clear contract and prevent API, client, and implementation drift.

## What Changes

- Create an initial set of OpenSpec capabilities that summarize the system's current documented behavior.
- Map existing documentation from `README.md`, `docs/design/`, `docs/desktop/`, client README files, and selected schema/config files into stable OpenSpec requirements.
- Establish a migration path where current behavior lives in `openspec/specs/` and future behavior changes go through `openspec/changes/`.
- Do not change application runtime behavior, API routes, database schema, or client implementation as part of this change.

## Capabilities

### New Capabilities
- `api-contract`: Common REST API response, error, pagination, authentication header, request tracing, and binary download contract.
- `identity-and-session`: User registration, login, password handling, JWT access tokens, refresh tokens, logout, revocation, and account protection behavior.
- `file-transfer`: Upload initialization, quota reservation, chunk upload, resumable upload, instant upload, upload completion/cancellation, and ranged download behavior.
- `file-namespace`: User file and folder namespace behavior, including listing, details, search, folder creation, rename, move, copy, and breadcrumb/tree navigation.
- `trash-lifecycle`: Soft delete, trash listing, restore, permanent delete, empty trash, expiry cleanup, and storage release semantics.
- `sharing`: Owner share management and public visitor access behavior, including share identifiers, passwords, share tokens, browse, download, and cancellation semantics.
- `admin-operations`: Administrator authorization, user management, role/status safety rules, share moderation, global storage statistics, and system overview behavior.
- `observability`: Health checks, system information, operation logs, request trace propagation, and operational status visibility.
- `runtime-configuration`: Startup configuration, secure configuration validation, PostgreSQL/Redis/storage integration, background tasks, and deployment-sensitive settings.
- `client-integration`: Cross-client API integration behavior for desktop, TUI, and web clients, including owner JWT and visitor share-token domains.

### Modified Capabilities

None. This is the initial OpenSpec baseline; no existing OpenSpec capabilities are being modified.

## Impact

- Adds OpenSpec planning and specification artifacts under `openspec/changes/bootstrap-openspec-specs/` and, when applied/synced, `openspec/specs/`.
- Uses existing documentation and code organization as sources of truth; no business code changes are intended.
- Future feature work should reference these specs and use delta specs for behavior changes instead of editing large design documents only.
