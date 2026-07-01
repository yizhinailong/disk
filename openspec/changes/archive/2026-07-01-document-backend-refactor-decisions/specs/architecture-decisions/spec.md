## ADDED Requirements

### Requirement: Backend refactor policy decisions
The architecture documentation SHALL preserve accepted backend refactor policy decisions for quota accounting, download metadata side effects, JWT ownership, and Redis rate-limit failure handling as durable decision records.

#### Scenario: Backend refactor decision is reviewed later
- **WHEN** a future reviewer revisits a backend refactor policy decision
- **THEN** the decision record SHALL show the current behavior context, accepted target decision, rationale, rejected alternatives, impact, and implementation follow-up status

#### Scenario: Decision differs from current implementation
- **WHEN** a backend refactor policy decision intentionally differs from current runtime behavior
- **THEN** the architecture documentation SHALL state that the decision does not change runtime behavior until a separate implementation change is applied

#### Scenario: Implementation change is proposed later
- **WHEN** a later change implements one of the accepted backend refactor policy decisions
- **THEN** that change SHALL cite the relevant decision record and SHALL include tests for the behavior it changes
