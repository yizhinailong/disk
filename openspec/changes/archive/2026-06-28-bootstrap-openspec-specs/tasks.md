## 1. Review Baseline Scope

- [x] 1.1 Review `proposal.md` and confirm the change is limited to bootstrapping OpenSpec specs from existing documentation.
- [x] 1.2 Confirm the capability list covers the first OpenSpec baseline without requiring runtime code, API route, database schema, or client behavior changes.
- [x] 1.3 Confirm existing `docs/design/`, `docs/desktop/`, README, SQL, and client documents remain as detailed background sources.

## 2. Review Capability Specs

- [x] 2.1 Review `api-contract` requirements for response envelopes, error codes, pagination, auth headers, binary downloads, and request tracing.
- [x] 2.2 Review `identity-and-session` requirements for registration, password handling, login, access tokens, refresh tokens, logout, and account protection.
- [x] 2.3 Review `file-transfer` requirements for upload initialization, storage reservation, instant upload, chunk upload, completion, cancellation, and ranged downloads.
- [x] 2.4 Review `file-namespace` requirements for listing, details, folder creation, rename, move, copy, search, tree, and breadcrumb behavior.
- [x] 2.5 Review `trash-lifecycle` requirements for soft delete, trash listing, restore, permanent delete, empty trash, and expiry cleanup.
- [x] 2.6 Review `sharing` requirements for share creation, external share identifiers, owner management, public access, share tokens, browse, and download behavior.
- [x] 2.7 Review `admin-operations` requirements for admin authorization, user administration, self-protection, last-admin protection, share moderation, and system statistics.
- [x] 2.8 Review `observability` requirements for health checks, system information, operation logs, request trace visibility, and background maintenance visibility.
- [x] 2.9 Review `runtime-configuration` requirements for configuration loading, secure validation, PostgreSQL, Redis, file storage, background task registration, and public route exemptions.
- [x] 2.10 Review `client-integration` requirements for REST compatibility, owner auth domain, visitor share domain, token refresh, upload workflow, and cross-client consistency.

## 3. Validate OpenSpec Format

- [x] 3.1 Ensure every capability listed in `proposal.md` has a corresponding `specs/<capability>/spec.md` file.
- [x] 3.2 Ensure each spec uses `## ADDED Requirements` because this is the initial OpenSpec baseline.
- [x] 3.3 Ensure every requirement has at least one `#### Scenario:` block using WHEN/THEN bullets.
- [x] 3.4 Run `openspec status --change "bootstrap-openspec-specs"` and confirm the change is apply-ready.

## 4. Prepare Sync And Follow-ups

- [x] 4.1 Identify any source-document or route mismatches discovered during review as follow-up change candidates instead of resolving them in this bootstrap change.
- [x] 4.2 Confirm the reviewed delta specs are ready to be synced into `openspec/specs/` using `/opsx:sync bootstrap-openspec-specs`.
- [x] 4.3 Confirm the change should be archived with `/opsx:archive bootstrap-openspec-specs` after the synced baseline is accepted.
