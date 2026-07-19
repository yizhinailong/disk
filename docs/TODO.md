# Parallel Client TODO for Codex Goal Mode

> Updated: 2026-07-19
>
> This file is the execution contract for four independent client-closure goals.
> Each goal must run in its own Codex chat and Git worktree. The goal text should
> point here instead of duplicating the full checklist, because `/goal` objectives
> are intentionally short while this file owns the detailed completion criteria.
>
> Completed implementation checklists and durable evidence belong in
> `docs/archive/`. Do not leave checked history in this file after all four issues
> are closed.

## 1. Current Baseline

- The completed backend refactor roadmap is archived at
  [`docs/archive/2026-07-14-backend-refactor-todo.md`](archive/2026-07-14-backend-refactor-todo.md).
- The self-contained backend CTest closure is archived at
  [`docs/archive/2026-07-18-ctest-self-contained.md`](archive/2026-07-18-ctest-self-contained.md).
- The backend implementation-marker audit is archived at
  [`docs/archive/2026-07-18-backend-implementation-marker-audit.md`](archive/2026-07-18-backend-implementation-marker-audit.md).
- The operation-specific share rate-limit closure is archived at
  [`docs/archive/2026-07-19-share-operation-rate-limits.md`](archive/2026-07-19-share-operation-rate-limits.md).
- No active backend behavior item remains. These four goals close already accepted
  client validation and documentation gaps; they do not authorize new backend API
  behavior.
- `openspec validate --all --strict --no-interactive` currently reports 22 passed
  and 2 failed. The only failures are the `TBD.` Purpose sections in
  `web-client-experience` and `desktop-client-experience`.
- Web store-level folder hierarchy coverage exists, but the Playwright fixtures
  still contain hard-coded identities and the file tests rely on pre-existing data,
  comments, and conditional assertions instead of deterministic setup.
- Desktop Qt unit coverage already proves visitor Range headers, restart behavior,
  size mismatch, and hash mismatch with mocked replies. It does not prove a real
  backend 206 response, persisted resume state, or final bytes on disk.
- DOC-00 through DOC-06 predate several QML component extractions and later drive
  features. Their paths, anchors, status labels, and duplicated planning entries
  need a code-and-test-backed audit.
- Web dependencies are not installed in the current checkout. `bun run typecheck`
  and `bun run test` therefore fail because `vue-tsc` and `vitest` are unavailable;
  this is an environment baseline, not accepted completion evidence.

## 2. Goal-Mode Operating Rules

Start each issue from the same baseline commit in a separate worktree. A suitable
goal prompt is:

```text
Complete <GOAL-ID> from docs/TODO.md and the linked GitHub issue. Stay inside its
owned file set, satisfy every acceptance check with real execution, and do not stop
until the definition of done is met or a concrete external blocker is recorded.
```

The following rules apply to every goal:

- One chat owns one goal. Do not ask one goal to opportunistically complete another.
- Do not let two worktrees edit the same files. `docs/TODO.md` is coordinator-owned;
  workers must not update checkboxes or issue links in it.
- Read the nearest `AGENTS.md` before editing. Documentation remains the authority
  for behavior; if a test exposes a need for new behavior rather than a defect in an
  accepted contract, stop and propose a separate OpenSpec change.
- Preserve owner and visitor authentication separation. Never store or print
  passwords, raw Share Tokens, authorization headers, or replayable credentials in
  source, logs, screenshots, traces, issue comments, or committed evidence.
- A skipped, conditionally bypassed, or environment-gated scenario is not a pass for
  the behavior it is meant to prove. Record the exact command and an unskipped pass.
- Keep fixes on existing code paths. Do not add parallel compatibility
  implementations when the current path can be corrected.
- Run focused verification first, then the broader suite named by the goal. Review
  the final diff for unrelated changes before declaring completion.
- Use Conventional Commits with a descriptive body and `Closes #<issue>` when the
  worker is asked to commit. Do not add AI or co-author signatures.

## 3. Parallel Work Map

| Goal | GitHub issue | Outcome | Exclusive primary ownership | Suggested branch |
|------|--------------|---------|-----------------------------|------------------|
| `WEB-E2E-001` | [#32](https://github.com/yizhinailong/disk/issues/32) | Deterministic real-browser folder-tree E2E against isolated backend data | `clients/disk-web/e2e/`; narrowly required Web test config, scripts, and semantic selectors | `test/32-web-folder-tree-e2e` |
| `DESKTOP-E2E-001` | [#33](https://github.com/yizhinailong/disk/issues/33) | Real-backend visitor download resume proof through the desktop transfer stack | `clients/desktop/tests/integration/`; narrowly required desktop test CMake and existing transfer/network code | `test/33-desktop-visitor-resume-e2e` |
| `CLIENT-SPEC-001` | [#34](https://github.com/yizhinailong/disk/issues/34) | Strictly valid Purpose text for both client OpenSpec capabilities | The two named files under `openspec/specs/` only | `docs/34-client-spec-purposes` |
| `DESKTOP-DOC-001` | [#35](https://github.com/yizhinailong/disk/issues/35) | DOC-00 through DOC-06 aligned with current QML/C++/test evidence | `docs/desktop/00-*.md` through `docs/desktop/06-*.md` only | `docs/35-desktop-authority-audit` |

All four goals have no implementation dependency on one another and may start
concurrently. Their primary owned files are disjoint. Shared generated/artifact
paths such as `build/`, `clients/desktop/build/`, Playwright output, screenshots,
traces, and `.sisyphus/evidence/` must not be committed.

---

## 4. `WEB-E2E-001` - Isolate and Prove Web Folder-Tree Workflows

### Goal objective

Make the Web folder-tree Playwright workflow deterministic against a real backend:
replace hard-coded identities, own and clean all test data, and prove navigation plus
hierarchy synchronization after create, rename, move, and delete operations.

### Authority and current evidence

- Normative behavior:
  [`openspec/specs/web-client-experience/spec.md`](../openspec/specs/web-client-experience/spec.md)
  under `Web Folder Tree Store Integration`.
- Current shared fixtures:
  [`clients/disk-web/e2e/fixtures.ts`](../clients/disk-web/e2e/fixtures.ts).
- Current browser scenarios:
  [`clients/disk-web/e2e/files.spec.ts`](../clients/disk-web/e2e/files.spec.ts).
- Store behavior and unit evidence:
  [`clients/disk-web/src/stores/drive.ts`](../clients/disk-web/src/stores/drive.ts) and
  [`clients/disk-web/src/stores/__tests__/drive.test.ts`](../clients/disk-web/src/stores/__tests__/drive.test.ts).

### Work contract

- [ ] Replace committed user/admin passwords and fixed identities in Playwright
  fixtures with environment-provided credentials or a deterministic seed interface.
  Missing required configuration must fail early with a useful, secret-free message.
- [ ] Give each run a unique namespace and create only the files/folders needed by
  the scenario. Teardown must remove only resources owned by that run, including
  cleanup after an assertion failure where Playwright supports it.
- [ ] Document one command sequence that starts from a clean backend data state,
  prepares prerequisites, runs the focused Chromium scenario, and performs cleanup.
- [ ] Replace data-dependent branches such as `if (rowCount > 0)` with explicit
  setup and unconditional assertions. A test with no matching row must fail, not pass.
- [ ] Prove tree selection, file-list contents, breadcrumb state, and active folder
  stay synchronized when navigating from the tree, file list, breadcrumb, and parent
  navigation supported by the current UI.
- [ ] Prove the visible hierarchy refreshes after folder create, rename, move, and
  delete. Use the existing store path; do not add a second tree-state mechanism.
- [ ] Preserve the last safe navigation state and surface a recoverable failure for
  the existing folder-tree refresh error path, using focused unit/browser evidence
  proportional to what can be observed deterministically.
- [ ] Add stable semantic selectors only where role/text selectors cannot express the
  contract reliably. Do not redesign the Web UI as part of this test goal.
- [ ] Ensure Playwright traces, screenshots, logs, and teardown output contain no
  credentials or raw authorization values.

### Verification and definition of done

- [ ] Install dependencies from the committed lockfile without silently rewriting it.
- [ ] `bun run typecheck` passes.
- [ ] `bun run test` passes.
- [ ] `bun run build` passes.
- [ ] The focused folder-tree Playwright command passes against a real backend with
  Chromium and contains no conditional skip for missing data.
- [ ] Run the focused scenario twice against the same backend and prove the second run
  is not affected by data left by the first.
- [ ] Review the final diff and confirm there is no backend behavior change, no secret,
  and no committed runtime artifact.

### Out of scope

Admin feature coverage, Web download behavior, backend API changes, and broad cleanup
of unrelated placeholder Playwright cases belong to separate work.

---

## 5. `DESKTOP-E2E-001` - Prove Visitor Resume Against a Real Backend

### Goal objective

Exercise the real desktop `TransferManager`/`NetworkClient`/`RequestFactory` path
against the backend and prove that a trusted partial visitor download sends a Range
request, receives HTTP 206, appends the remaining bytes, and passes final size and
hash verification.

### Authority and current evidence

- Desktop contract:
  [`openspec/specs/desktop-client-experience/spec.md`](../openspec/specs/desktop-client-experience/spec.md)
  under visitor resume and integrity requirements.
- Cross-client contract:
  [`openspec/specs/client-integration/spec.md`](../openspec/specs/client-integration/spec.md)
  under resumable download integration and completion verification.
- Existing mocked coverage:
  [`clients/desktop/tests/unit/transfers/test_transfer_manager.cpp`](../clients/desktop/tests/unit/transfers/test_transfer_manager.cpp).
- Production path:
  [`clients/desktop/src/managers/TransferManager.cpp`](../clients/desktop/src/managers/TransferManager.cpp),
  [`clients/desktop/src/network/NetworkClient.cpp`](../clients/desktop/src/network/NetworkClient.cpp), and
  [`clients/desktop/src/network/RequestFactory.cpp`](../clients/desktop/src/network/RequestFactory.cpp).

### Work contract

- [ ] Add a desktop integration harness under
  `clients/desktop/tests/integration/` that uses a real
  `QNetworkAccessManager`; mocked replies cannot satisfy this goal.
- [ ] Provision an isolated owner, deterministic file payload, downloadable share,
  visitor access token, local partial file, and matching persisted resume metadata
  using existing backend contracts. Clean remote and local fixtures on exit.
- [ ] Drive the download through the production desktop transfer classes rather than
  reimplementing Range logic in the test orchestrator.
- [ ] Observe and assert `Range: bytes=<partial-size>-`, visitor-only
  `X-Share-Token` authentication, and HTTP 206 without persisting the raw token in
  evidence. Assert that no owner `Authorization` header is sent on the visitor request.
- [ ] Assert the final file equals the original deterministic payload by byte size and
  available hash, the task reports ranged transfer and completed verification, and
  the resume sidecar is removed only after successful completion.
- [ ] Register a named, serial test or provide an equally deterministic one-command
  runner. It may retain an explicit environment gate for normal developer suites,
  but this issue cannot close until an unskipped real-backend run passes.
- [ ] Keep current unit tests passing. If the real test exposes a desktop defect, fix
  the existing transfer/network path narrowly and add a regression assertion. Do not
  change backend semantics inside this goal.
- [ ] Prevent passwords, raw Share Tokens, authorization headers, and local absolute
  secret-bearing paths from appearing in CTest output or committed evidence.

### Verification and definition of done

- [ ] Configure and build the desktop project with
  `cmake -S clients/desktop -B clients/desktop/build` and
  `cmake --build clients/desktop/build`.
- [ ] `ctest --test-dir clients/desktop/build -R desktop-unit-tests -V` passes.
- [ ] The named visitor-resume integration command runs unskipped against a real
  backend and passes all request, 206, disk-byte, size, hash, state, and cleanup
  assertions.
- [ ] Repeat the integration scenario once to prove fixture isolation and deterministic
  cleanup.
- [ ] Review the final diff for advisory-only desktop warnings, auth-domain separation,
  absence of backend behavior changes, secrets, and runtime artifacts.

### Out of scope

Desktop UI redesign, owner-download feature expansion, backend Range changes, and
replacement of the existing transfer state machine are not authorized.

---

## 6. `CLIENT-SPEC-001` - Complete Client OpenSpec Purpose Sections

### Goal objective

Replace the two client `TBD.` Purpose placeholders with concise capability summaries
that accurately describe the existing requirements and make aggregate strict
OpenSpec validation pass 24 of 24.

### Exclusive files

- [`openspec/specs/web-client-experience/spec.md`](../openspec/specs/web-client-experience/spec.md)
- [`openspec/specs/desktop-client-experience/spec.md`](../openspec/specs/desktop-client-experience/spec.md)

### Work contract

- [ ] Write a Web Purpose that covers the capability represented by its current
  requirements: administrator quota editing, centralized folder-tree synchronization,
  and memory-safe browser downloads.
- [ ] Write a Desktop Purpose that covers the current Qt/QML shell, authentication,
  explorer/navigation/interaction, traceability, administrator, Chinese terminology,
  visitor resume, and integrity contracts at capability level.
- [ ] Keep each Purpose concise and normative in scope. Do not turn it into an
  implementation inventory or duplicate individual scenarios.
- [ ] Do not add, remove, rename, or alter any Requirement or Scenario in this goal.
- [ ] Do not use this documentation-only issue to claim that planned desktop behavior
  is implemented.

### Verification and definition of done

- [ ] `openspec validate web-client-experience --type spec --strict --no-interactive`
  passes.
- [ ] `openspec validate desktop-client-experience --type spec --strict --no-interactive`
  passes.
- [ ] `openspec validate --all --strict --no-interactive` reports 24 passed and 0
  failed.
- [ ] `rg -n '^TBD\\.$' openspec/specs/web-client-experience/spec.md
  openspec/specs/desktop-client-experience/spec.md` returns no match.
- [ ] The final diff contains only the two Purpose replacements in the exclusive
  files above.

### Out of scope

Requirement changes, client implementation, legacy narrative documentation, and
historical archived OpenSpec changes are excluded.

---

## 7. `DESKTOP-DOC-001` - Re-audit DOC-00 Through DOC-06

### Goal objective

Reconcile the desktop authority series DOC-00 through DOC-06 with the current QML,
C++, and executable tests: repair stale paths and anchors, correct status labels, and
deduplicate overlapping planned entries without promoting unverified behavior.

### Exclusive files

- `docs/desktop/00-桌面客户端系统概述与文档治理.md`
- `docs/desktop/01-信息架构与功能视图.md`
- `docs/desktop/02-页面布局与交互规范.md`
- `docs/desktop/03-状态模型与导航模型.md`
- `docs/desktop/04-组件页面与实现映射.md`
- `docs/desktop/05-文档验证计划.md`
- `docs/desktop/06-验证用例与迁移矩阵.md`

### Work contract

- [ ] Inventory the current desktop `qml/pages/`, `qml/shells/`,
  `qml/components/drive/`, `src/app/`, `src/managers/`, `src/models/`,
  `src/network/`, `tests/unit/`, and `tests/quick/` trees before changing claims.
- [ ] Recheck every `[已实现]`, `[规划]`, and `[待验证]` claim in DOC-00 through
  DOC-06 against a current code anchor and proportionate test evidence.
- [ ] Correct stale monolithic `DriveBrowserPage.qml` anchors after the split into
  `DriveMyFilesView`, `DriveSharedView`, `DriveTrashView`, context-menu/dialog, and
  related drive components.
- [ ] Reconcile currently contradictory claims around search, sorting, list/grid
  layout, context-menu actions, multi-selection, shared/trash view modes, and current
  page state. Existing properties or signals alone do not prove complete UI behavior;
  use rendered QML wiring plus tests.
- [ ] Keep visible owner-file pagination/load-more, internal drag/drop movement,
  external drag/drop upload, and loading skeletons as planned unless current code and
  executable tests prove the documented user-visible contract end to end.
- [ ] Deduplicate planned entries that describe the same missing behavior in multiple
  matrices while preserving one authoritative requirement and valid cross-references.
- [ ] Update DOC-05 validation commands and DOC-06 evidence/migration mappings so
  they target the current component and test layout and can be executed from the
  repository root.
- [ ] Repair broken document links, IDs, terminology, status counts, and implementation
  anchors discovered by the audit. Do not edit DOC-07, DOC-08, OpenSpec, QML, C++, or
  tests in this issue; record a follow-up when evidence outside the exclusive files is
  genuinely wrong.

### Verification and definition of done

- [ ] Run every applicable structural, taxonomy, anchor, cross-reference, and status
  validation command defined by the updated DOC-05; all P0/P1 checks pass.
- [ ] Search the seven files for every referenced desktop source/test path and confirm
  each path exists, with explicit exceptions only for entries intentionally marked
  `[规划]` and described as absent.
- [ ] Sample every promoted `[已实现]` claim and confirm both a current implementation
  anchor and executable test evidence are cited. No feature is promoted from a stale
  line number or a manager signal alone.
- [ ] `git diff --exit-code -- clients/desktop openspec` succeeds, proving the issue
  remained documentation-only outside its exclusive files.
- [ ] Review the final diff for duplicate planning rows, contradictory statuses, stale
  anchors, and unrelated prose churn.

### Out of scope

Implementing planned desktop features, rewriting DOC-07 terminology, changing the
DOC-08 administrator design, and editing OpenSpec Purpose text are separate concerns.

---

## 8. Integration and Closure

The four issues can merge in any order because their owned files do not overlap.
`CLIENT-SPEC-001` will make aggregate OpenSpec validation green early, but no other
goal depends on that result. Before closing the parent work:

- [ ] All four GitHub issues are closed by merged, reviewed changes with their focused
  verification recorded.
- [ ] `openspec validate --all --strict --no-interactive` reports 24 passed and 0
  failed.
- [ ] The Web typecheck, unit suite, build, and deterministic folder-tree Playwright
  scenario pass from an installed, lockfile-consistent environment.
- [ ] Desktop unit/quick tests and the unskipped real-backend visitor resume scenario
  pass.
- [ ] No worker changed another goal's exclusive files, committed generated artifacts,
  or exposed credentials in source, Git history, logs, or evidence.
- [ ] Create a dated closure/evidence note under `docs/archive/`, then replace this
  active checklist with the next verified unfinished work rather than retaining
  checked history.

## 9. Non-active Product Parking Lot

The following are not accepted implementation goals and deliberately have no
checkboxes: visible owner-file pagination/load-more behavior, internal drag/drop
movement, external drag/drop upload policy, and loading skeletons. Each requires an
explicit product decision and OpenSpec proposal before implementation. The DOC audit
may correct their documented status, but it must not implement or silently accept
them.
