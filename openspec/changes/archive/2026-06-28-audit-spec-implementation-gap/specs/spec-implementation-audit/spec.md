## ADDED Requirements

### Requirement: Capability audit inventory
The system SHALL provide a point-in-time audit inventory that covers every main OpenSpec capability and every requirement within those capabilities.

#### Scenario: All capabilities are included
- **WHEN** the implementation gap audit is produced
- **THEN** every capability under `openspec/specs/` is represented in the audit inventory

#### Scenario: Every requirement receives a status
- **WHEN** a capability contains one or more requirements
- **THEN** each requirement is assigned an explicit audit status instead of being omitted

### Requirement: Implementation coverage classification
The system SHALL classify implementation coverage for each audited requirement using explicit statuses: `implemented`, `partial`, `not-implemented`, `ambiguous`, or `not-audited`.

#### Scenario: Requirement has direct implementation evidence
- **WHEN** source evidence demonstrates the required behavior
- **THEN** the requirement is classified as `implemented` or `partial` with evidence references

#### Scenario: Requirement has no discovered implementation evidence
- **WHEN** the audit finds no implementation evidence for a requirement after searching relevant areas
- **THEN** the requirement is classified as `not-implemented` with notes describing the search scope

#### Scenario: Requirement cannot be judged from the spec
- **WHEN** requirement wording is too ambiguous to determine implementation coverage
- **THEN** the requirement is classified as `ambiguous` with a recommended clarification

### Requirement: Verification coverage classification
The system SHALL classify test or verification coverage separately from implementation coverage for each audited requirement.

#### Scenario: Requirement has automated test evidence
- **WHEN** automated tests verify the requirement behavior
- **THEN** the audit records the test evidence separately from implementation evidence

#### Scenario: Requirement lacks test evidence
- **WHEN** implementation evidence exists but no test or verification evidence is found
- **THEN** the audit marks verification coverage as missing or partial without downgrading implementation coverage automatically

### Requirement: Evidence-based findings
The system SHALL attach evidence to each audit conclusion unless the requirement is explicitly marked `not-audited`.

#### Scenario: Audit marks a requirement implemented
- **WHEN** a requirement is marked `implemented`
- **THEN** the audit includes file, symbol, test, or command evidence supporting that conclusion

#### Scenario: Audit marks a requirement not implemented
- **WHEN** a requirement is marked `not-implemented`
- **THEN** the audit includes notes about where implementation evidence was searched for and not found

### Requirement: Prioritized follow-up recommendations
The system SHALL produce prioritized recommendations for follow-up OpenSpec changes based on audited gaps.

#### Scenario: Gaps are found
- **WHEN** the audit identifies partial, missing, ambiguous, or unverified requirements
- **THEN** the audit groups related gaps into proposed follow-up changes with priority rationale

#### Scenario: High-risk gaps are found
- **WHEN** a gap affects correctness, data safety, access control, or externally visible API behavior
- **THEN** the audit marks it as high priority unless a documented rationale explains otherwise
