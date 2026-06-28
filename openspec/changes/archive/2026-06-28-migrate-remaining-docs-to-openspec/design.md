## Context

The repository now has a first OpenSpec baseline for backend/domain capabilities, but several detailed documentation sets still sit outside OpenSpec as separate authorities. The largest remaining gap is the desktop client documentation series in `docs/desktop/`, followed by backend support documents for persistence design, validation, deployment operations, and architectural decisions.

This change is a documentation/specification migration. It should add capability-oriented OpenSpec specs that summarize stable requirements from the remaining documents, while leaving runtime code, API behavior, database schema, deployment scripts, and client behavior unchanged.

## Goals / Non-Goals

**Goals:**

- Convert the remaining stable documentation areas into OpenSpec capability specs.
- Preserve the existing backend/domain specs as the current baseline and avoid unnecessary requirement modifications.
- Make OpenSpec the primary place to review future requirement changes for desktop UX, documentation governance, persistence design, validation, deployment, and architecture decisions.
- Keep source documents available as migration inputs and detailed historical/background references.

**Non-Goals:**

- Do not implement new desktop, backend, database, or operations behavior.
- Do not delete or rewrite legacy documentation as part of proposing this change.
- Do not prove every legacy documentation claim against the current implementation during proposal creation.
- Do not duplicate every table, route matrix, or historical paragraph from the source documents into OpenSpec.

## Source Inventory and Classification

The migration uses the following remaining documentation sources as inputs. These sources remain reference material unless their stable requirements are summarized into the capability specs in this change.

| Source | Capability coverage |
|--------|---------------------|
| `docs/design/00-系统概述.md` | `documentation-governance`, `persistence-design`, `validation-and-performance`, `deployment-operations` |
| `docs/design/01-功能需求规格.md` | Existing backend/domain specs, `validation-and-performance` as supporting context |
| `docs/design/02-API接口设计.md` | Existing `api-contract` and backend/domain specs, `desktop-client-experience` API mapping context |
| `docs/design/03-数据库设计.md` | `persistence-design`, `deployment-operations` |
| `docs/design/04-系统测试计划.md` | `validation-and-performance` |
| `docs/design/05-部署运维指南.md` | `deployment-operations` |
| `docs/design/06-单元测试用例.md` | `validation-and-performance` |
| `docs/design/07-压力测试.md` | `validation-and-performance` |
| `docs/design/ADR-001-PostgreSQL迁移决策.md` | `architecture-decisions`, `persistence-design` |
| `docs/design/async-streaming-eval.md` | `architecture-decisions` |
| `docs/design/io-uring-analysis.md` | `architecture-decisions` |
| `docs/desktop/00-桌面客户端系统概述与文档治理.md` | `desktop-client-experience`, `documentation-governance` |
| `docs/desktop/01-信息架构与功能视图.md` | `desktop-client-experience` |
| `docs/desktop/02-页面布局与交互规范.md` | `desktop-client-experience` |
| `docs/desktop/03-状态模型与导航模型.md` | `desktop-client-experience` |
| `docs/desktop/04-组件页面与实现映射.md` | `desktop-client-experience` |
| `docs/desktop/05-文档验证计划.md` | `validation-and-performance`, `documentation-governance` |
| `docs/desktop/06-验证用例与迁移矩阵.md` | `validation-and-performance`, `documentation-governance` |
| `docs/desktop/07-中文UI术语表.md` | `desktop-client-experience` |
| `docs/desktop/08-管理员功能设计.md` | `desktop-client-experience` |
| `docs/lunwen/基于Drogon的网络磁盘系统的设计与实现_毕业论文.md` and selected `docs/lunwen/assets/` diagrams/screenshots | `desktop-client-experience`, `persistence-design`, `validation-and-performance`, `architecture-decisions` as historical thesis/reference context |
| `README.md` | `documentation-governance`, `deployment-operations`, `validation-and-performance` as repository overview context |
| `clients/disk-tui/README.md` | `documentation-governance`, existing backend/API capabilities, and client integration reference context |

Claims in thesis or legacy-style sources that still mention MySQL, older endpoint variants, implementation status, or aspirational UX remain follow-up reconciliation candidates. This proposal does not change code, schema, API behavior, deployment behavior, or client runtime behavior to match those claims.

## Decisions

### Decision: Add missing areas as new capabilities

The change introduces new capabilities for `desktop-client-experience`, `documentation-governance`, `persistence-design`, `validation-and-performance`, `deployment-operations`, and `architecture-decisions` instead of modifying the existing backend/domain capabilities.

**Rationale:** Existing specs already cover backend behavior such as identity, file transfer, sharing, trash, observability, runtime configuration, and API contracts. The remaining documents describe adjacent concerns that need their own stable anchors.

**Alternative considered:** Modify existing specs such as `client-integration`, `runtime-configuration`, and `observability`. This was rejected because it would blur product UX, governance, schema, validation, deployment, and ADR concerns into broader capabilities that are harder to review.

### Decision: Keep migration requirements behavior-preserving

The specs describe current documented contracts and governance expectations without requiring application behavior changes in this proposal.

**Rationale:** A documentation migration should be reviewable without running code migrations or changing user-facing behavior. Any discovered mismatch between docs and implementation should become a separate follow-up change.

**Alternative considered:** Combine migration with implementation reconciliation. This was rejected because it would make the OpenSpec migration harder to review and could hide behavior changes inside documentation work.

### Decision: Summarize source documents into testable requirements

Each capability spec should contain concise SHALL requirements and `#### Scenario:` blocks. Detailed matrices, source-specific prose, and historical notes remain in the original documents unless they define a stable requirement.

**Rationale:** OpenSpec is most useful when specs are focused, testable, and easy to modify via future delta specs.

### Decision: Treat legacy documents as inputs during the migration

The migration should reference `docs/design/`, `docs/desktop/`, `docs/lunwen/`, README files, and existing OpenSpec specs as inputs, but the resulting OpenSpec specs should stand on their own as requirement contracts.

**Rationale:** This keeps OpenSpec independent of the old document hierarchy while preserving traceability to source material for reviewers.

## Risks / Trade-offs

- **Risk: Requirements become too broad** → Mitigation: Keep the first migration pass concise and split capabilities later when real changes need finer granularity.
- **Risk: Legacy documents contain stale or aspirational claims** → Mitigation: Mark this change as behavior-preserving and create follow-up reconciliation changes for mismatches.
- **Risk: Overlap with existing backend specs** → Mitigation: Add only missing adjacent concerns and avoid restating backend API, auth, file, sharing, trash, or observability requirements already covered.
- **Risk: Multiple sources of truth remain temporarily** → Mitigation: Add documentation-governance requirements that define OpenSpec as the primary future requirement source after migration.

## Migration Plan

1. Create proposal, design, delta specs, and implementation tasks for the migration.
2. Review each new capability spec against its source documents for missing or duplicated concerns.
3. Apply the change by finalizing the delta specs and marking tasks complete.
4. Sync accepted specs into `openspec/specs/`.
5. Create follow-up changes for source-document archival, links, or doc/code reconciliation if needed.

Rollback is documentation-only: revert the change artifacts before sync, or revert the synced spec files after sync if the capability split needs to be redesigned.

## Open Questions

None for proposal creation. Any discovered stale source-document claim should be tracked as a follow-up rather than blocking this migration proposal.
