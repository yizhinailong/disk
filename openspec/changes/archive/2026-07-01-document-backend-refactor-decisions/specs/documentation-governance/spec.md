## ADDED Requirements

### Requirement: Backend refactor decision documentation
Documentation governance SHALL keep backend refactor decision notes traceable to the active backend roadmap and SHALL distinguish confirmed current behavior from accepted target decisions and implementation follow-up work.

#### Scenario: Backend decision note is consulted
- **WHEN** a maintainer reviews `docs/backend-refactor-decisions.md`
- **THEN** the document SHALL identify whether each covered item is current implementation behavior, an accepted target decision, or a later implementation requirement

#### Scenario: Backend roadmap is updated after decisions
- **WHEN** `docs/TODO.md` lists backend refactor tasks covered by the backend decision note
- **THEN** decision-only checklist items SHALL link to the decision note while behavior-changing implementation tasks SHALL remain open until implemented and tested

#### Scenario: Discovery and decision documents disagree
- **WHEN** `docs/backend-discovery.md` records current behavior that differs from an accepted backend refactor decision
- **THEN** documentation SHALL preserve both facts by labeling discovery as current behavior and the decision note as target behavior for future implementation
