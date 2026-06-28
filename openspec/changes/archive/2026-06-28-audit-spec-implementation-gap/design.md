## Context

The repository has an established OpenSpec source of truth with many main capabilities, including API contracts, file namespace behavior, file transfer, sharing, trash lifecycle, identity/session behavior, persistence, observability, deployment, and client integration. Recent work also introduced a Go TUI client intended to cover the backend API surface.

Because the specification surface is now broad, the project needs a repeatable audit method that answers three questions before the next implementation change is selected:

1. Which requirements are fully implemented and verified?
2. Which requirements are partially implemented, untested, or ambiguous?
3. Which missing pieces should become the next OpenSpec changes?

This change is intentionally read-only with respect to product behavior. It defines the audit output and the work required to produce it.

## Goals / Non-Goals

**Goals:**

- Map every main OpenSpec capability and requirement to implementation evidence where available.
- Distinguish implementation coverage from test coverage.
- Identify unsupported, partially supported, ambiguous, and risky requirements.
- Produce a prioritized follow-up list suitable for new OpenSpec changes.
- Keep findings evidence-based by referencing files, symbols, tests, or explicit absence after search.

**Non-Goals:**

- Implement missing product features discovered by the audit.
- Change existing API, persistence, client, deployment, or runtime behavior.
- Rewrite existing specs during the audit unless the audit uncovers a separate spec-quality change that should be proposed later.
- Treat undocumented implementation behavior as a replacement for OpenSpec requirements.

## Decisions

### Use a capability-by-capability audit matrix

The audit SHALL produce a matrix organized by OpenSpec capability and requirement rather than by source file. This keeps the source of truth centered on intended behavior and makes gaps visible even when no implementation exists.

Alternative considered: organize by backend/client modules. That would be easier to produce from code search, but it risks hiding requirements that have no code counterpart.

### Separate status dimensions

Each requirement SHALL track implementation status separately from verification status. A requirement can be implemented but untested, tested only through a client path, or tested without covering important edge cases.

Alternative considered: a single pass/fail status. That would be simpler but would not tell whether the next step is coding, testing, spec clarification, or documentation.

### Require evidence for every non-unknown conclusion

The audit SHALL include evidence references for implemented, partial, tested, and unimplemented conclusions. Evidence can include source files, tests, command output, or explicit search notes.

Alternative considered: rely on reviewer judgment without references. That would be faster but not repeatable enough to guide future changes.

### Rank follow-up work by product value and dependency order

The final recommendations SHALL group gaps into proposed follow-up changes and rank them by user-visible value, correctness risk, dependency impact, and ease of verification.

Alternative considered: list gaps without ranking. That would preserve neutrality but would not answer the practical question of what to do next.

## Risks / Trade-offs

- Audit drift → The matrix can become stale as implementation changes. Mitigation: treat the audit as a point-in-time artifact and refresh it before starting major follow-up work.
- False confidence from shallow evidence → A file reference may not prove full behavior. Mitigation: require notes about edge cases and verification scope, not just code locations.
- Overly broad audit scope → Covering every capability may take longer than expected. Mitigation: allow staged completion but require explicit `not audited` status rather than silence.
- Ambiguous requirements → Some specs may not be directly testable. Mitigation: flag them as spec-clarification candidates instead of forcing implementation conclusions.
