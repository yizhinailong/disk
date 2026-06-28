## 1. Audit Setup

- [x] 1.1 Enumerate all capability specs under `openspec/specs/` and extract every requirement and scenario into an audit inventory.
- [x] 1.2 Define the audit matrix columns for capability, requirement, implementation status, verification status, evidence, gaps, risk, and recommended follow-up.
- [x] 1.3 Identify relevant implementation and test areas for backend, clients, persistence, deployment, and operational behavior.

## 2. Capability Coverage Review

- [x] 2.1 Audit API, identity/session, runtime configuration, and architecture-decision requirements against implementation and tests.
- [x] 2.2 Audit file namespace, file transfer, sharing, trash lifecycle, and persistence requirements against implementation and tests.
- [x] 2.3 Audit client integration, desktop client experience, admin operations, deployment operations, observability, documentation governance, and validation/performance requirements against implementation and tests.
- [x] 2.4 Mark every requirement with one implementation status: `implemented`, `partial`, `not-implemented`, `ambiguous`, or `not-audited`.
- [x] 2.5 Mark verification coverage separately for every requirement and cite test or manual verification evidence when present.

## 3. Evidence and Gap Analysis

- [x] 3.1 Attach source, symbol, test, command, or search-scope evidence to every audited conclusion.
- [x] 3.2 Identify requirements that need spec clarification because implementation coverage cannot be judged from current wording.
- [x] 3.3 Identify implemented-but-unverified behavior that needs test coverage before being considered complete.
- [x] 3.4 Identify high-risk gaps affecting correctness, data safety, access control, externally visible API behavior, or deployment safety.

## 4. Recommendations and Verification

- [x] 4.1 Group related gaps into proposed follow-up OpenSpec changes with concise scope statements.
- [x] 4.2 Rank proposed follow-up changes by user value, correctness risk, dependency impact, and ease of verification.
- [x] 4.3 Produce the final audit report and confirm every main capability is represented.
- [x] 4.4 Run OpenSpec validation/status checks and record any validation issues or follow-up cleanup needed.
