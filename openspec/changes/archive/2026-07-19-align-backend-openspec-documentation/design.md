## Context

The five backend documentation capabilities migrated into OpenSpec with placeholder Purpose text. Separately, `documentation-governance` gained a requirement while the backend refactor roadmap was active; that roadmap has since completed and moved to `docs/archive/2026-07-14-backend-refactor-todo.md`, while `docs/TODO.md` now tracks only verified current work. The capability metadata and governance contract must reflect the current documentation lifecycle without changing runtime behavior.

## Goals / Non-Goals

**Goals:**

- Give each active backend documentation capability a concise statement of its authoritative scope.
- Keep OpenSpec authoritative for migrated requirements.
- Distinguish the active backend backlog, current decision status, completed roadmap, historical discovery, and supporting design sources.
- Preserve traceability from current work and requirements to relevant decision, design, and historical context.
- Introduce the governance correction through a complete delta before synchronizing the active spec.

**Non-Goals:**

- No application, API, database, runtime configuration, dependency, deployment, or test behavior changes.
- No changes to Web or Desktop client capability purposes or client documentation.
- No rewrite of the detailed legacy design, decision, discovery, TODO, or archive documents.

## Decisions

### Decision: Purpose text states authoritative scope

Each Purpose will be one concise sentence that identifies the capability's authoritative range without repeating its requirements. The wording will be long enough to satisfy strict OpenSpec validation.

Alternative considered: summarize every requirement in each Purpose. Rejected because that duplicates the contract, increases drift risk, and differs from established capability style.

### Decision: Documentation sources retain explicit lifecycle roles

Active OpenSpec capability specs are the normative authority for migrated requirements. `docs/backend-refactor-decisions.md` records current decisions and implementation status; `docs/TODO.md` contains verified unfinished backend work; `docs/archive/2026-07-14-backend-refactor-todo.md` is the completed roadmap; `docs/backend-discovery.md` is the historical discovery baseline; and `docs/design/` remains supporting reference material.

Alternative considered: continue treating `docs/TODO.md` as both current backlog and completed roadmap history. Rejected because the repository deliberately separated those roles and duplicate histories become stale.

### Decision: Governance semantics flow through a delta

The complete `Backend refactor decision documentation` requirement will be modified in this change before its accepted text is synchronized into the active `documentation-governance` spec.

Alternative considered: edit only the active spec. Rejected because it would bypass the repository's OpenSpec change history for a normative governance change.

### Decision: Synchronize the active spec in this implementation

After the delta validates, the same complete requirement will replace the stale active requirement. Keeping both blocks textually identical makes the review and future archival behavior unambiguous.

Alternative considered: leave the active spec stale until a separate archive operation. Rejected because the issue's completion condition requires the active governance contract to distinguish current and archived work now.

## Risks / Trade-offs

- [Risk] Purpose text could restate or broaden requirement semantics. → Mitigation: limit each Purpose to the capability's existing authoritative scope and validate it against the full requirement body.
- [Risk] Decision, discovery, TODO, and archive roles could still overlap. → Mitigation: name each source's role explicitly in the requirement and its scenarios while retaining OpenSpec as normative.
- [Risk] Delta and active requirement could diverge. → Mitigation: synchronize the complete validated block verbatim and compare both during final review.
- [Risk] Strict repository-wide validation remains nonzero because two deferred client purposes are still placeholders. → Mitigation: require strict success for the five affected backend specs and report only the two explicit client exclusions in the aggregate result.

## Migration Plan

1. Create the OpenSpec proposal, design, complete governance delta, and task checklist.
2. Strictly validate the change before editing the active governance requirement.
3. Replace the five backend Purpose placeholders and synchronize the validated governance block into the active spec.
4. Validate the change, affected capabilities, aggregate specs, source paths, and final diff.
5. Roll back by reverting these documentation-only OpenSpec edits if the governance model is rejected.

## Open Questions

- None.
