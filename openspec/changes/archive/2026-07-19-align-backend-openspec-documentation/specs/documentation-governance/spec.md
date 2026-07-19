## MODIFIED Requirements

### Requirement: Backend refactor decision documentation
Documentation governance SHALL keep backend refactor decisions traceable to their current status, the completed historical roadmap, and any verified active follow-up work. OpenSpec SHALL remain the normative authority for migrated requirements, while decision, backlog, archive, discovery, and design sources SHALL retain the roles defined by the scenarios below.

#### Scenario: Backend decision note is consulted
- **WHEN** a maintainer reviews `docs/backend-refactor-decisions.md`
- **THEN** the documentation SHALL distinguish accepted decisions, current implementation status, and any remaining follow-up work, SHALL trace completed roadmap history to `docs/archive/2026-07-14-backend-refactor-todo.md`, and SHALL trace only verified current unfinished work to `docs/TODO.md`

#### Scenario: Completed backend roadmap is consulted
- **WHEN** a maintainer needs the completed backend refactor sequence or its historical checklist
- **THEN** documentation SHALL identify `docs/archive/2026-07-14-backend-refactor-todo.md` as the historical completion record and SHALL NOT describe that roadmap as active work

#### Scenario: Current backend backlog references a decision
- **WHEN** `docs/TODO.md` lists verified unfinished backend work related to an accepted decision
- **THEN** the backlog SHALL retain only the current open work and SHALL link the relevant OpenSpec requirement, `docs/design/` source, or decision record without recreating completed roadmap tasks as active items

#### Scenario: Discovery and current decision status disagree
- **WHEN** `docs/backend-discovery.md` describes behavior that differs from the current status in `docs/backend-refactor-decisions.md`
- **THEN** documentation SHALL identify discovery as a historical observation, use the decision note for current decision and implementation status, and use OpenSpec as the normative requirement authority
