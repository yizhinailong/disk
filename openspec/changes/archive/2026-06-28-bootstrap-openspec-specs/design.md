## Context

The repository already contains detailed human-oriented documentation for the Disk network-drive system: backend overview, functional requirements, API contracts, database design, deployment guidance, test plans, desktop product documentation, and client README files. The OpenSpec project has been initialized with the `spec-driven` schema, but `openspec/specs/` is empty.

This change bootstraps OpenSpec by extracting current stable behavior from the existing documentation into capability-oriented requirements. The goal is not to replace the existing design documents immediately; it is to create a concise behavioral contract that future OpenSpec changes can modify through delta specs.

## Goals / Non-Goals

**Goals:**

- Create an initial OpenSpec baseline for the documented system behavior.
- Split large existing documents into capability-oriented specs rather than one spec per source document.
- Keep OpenSpec requirements focused on externally observable behavior, invariants, and cross-component contracts.
- Preserve existing `docs/design/`, `docs/desktop/`, README, SQL, and code files as sources of background detail.
- Make future changes easier to review by giving them stable capability names and requirement anchors.

**Non-Goals:**

- Do not change backend, client, database, or deployment behavior.
- Do not copy every paragraph from the existing documentation into OpenSpec.
- Do not encode low-level implementation details unless they define a stable system contract.
- Do not remove or rewrite the existing design documentation in this change.
- Do not attempt to prove that every documented behavior exactly matches implementation; mismatches should become future follow-up changes.

## Decisions

### Decision: Use capability-oriented specs instead of document-oriented specs

OpenSpec specs will be organized by system capability, such as `file-transfer` and `sharing`, rather than by source document, such as `functional-requirements` or `api-design`.

**Rationale:** The existing documents are broad and overlapping. Capability-oriented specs make future changes smaller and allow delta specs to target the behavior being changed.

**Alternative considered:** Convert each existing document into one OpenSpec spec. This was rejected because it would preserve the current document boundaries instead of creating useful behavioral contracts.

### Decision: Start with baseline ADDED requirements

Because `openspec/specs/` is empty, every initial capability is introduced using `## ADDED Requirements` in the change specs.

**Rationale:** This establishes main specs during sync without implying changes to existing OpenSpec requirements.

**Alternative considered:** Write final main specs directly under `openspec/specs/`. This was rejected because using a change preserves an auditable migration path and allows review before sync/archive.

### Decision: Keep requirements concise and scenario-driven

Each requirement should capture stable behavior with one or more `#### Scenario:` examples. Detailed field matrices, database indexes, route tables, and historical notes stay in the existing documentation unless they define a normative behavior.

**Rationale:** OpenSpec works best when requirements are testable and change-focused. Overly long specs would be hard to maintain and would duplicate existing docs.

### Decision: Treat documentation/code mismatches as follow-up changes

This bootstrap captures currently documented behavior at a high level. If route coverage, DTO fields, or implementation details differ across README, design docs, clients, and code, those differences should be identified and resolved through future OpenSpec changes.

**Rationale:** The first baseline should avoid mixing documentation migration with behavior correction.

## Risks / Trade-offs

- **Risk: Specs may be too broad initially** → Mitigation: Keep first-pass requirements high-level and split later when a change needs finer granularity.
- **Risk: Existing documentation may contain stale behavior** → Mitigation: Treat the baseline as a migration artifact and create follow-up reconciliation changes for route/API mismatches.
- **Risk: Duplicated sources of truth during transition** → Mitigation: Keep existing docs as detailed background while making OpenSpec the stable behavior contract for future changes.
- **Risk: Too many capabilities at once** → Mitigation: Use concise requirements per capability and prioritize future refinement of `api-contract`, `identity-and-session`, `file-transfer`, and `file-namespace` first.

## Migration Plan

1. Create this change with proposal, design, capability delta specs, and tasks.
2. Review the initial capability split and requirement wording.
3. Apply the change by verifying/adjusting the generated specs and marking tasks complete.
4. Sync the delta specs into `openspec/specs/`.
5. Archive the change after the baseline is accepted.
6. Create follow-up changes for any discovered doc/code/API mismatches.

Rollback is simple because no runtime code is changed: remove or revert the OpenSpec change artifacts before sync, or revert the synced `openspec/specs/` files after sync if the baseline needs to be redesigned.
