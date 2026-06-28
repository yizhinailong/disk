## Why

The project now has a broad set of OpenSpec capabilities, but there is no explicit, repeatable view of which requirements are already implemented, tested, partially covered, or still missing. Before choosing the next implementation change, we need a grounded gap audit that connects specs to actual backend, client, test, and operational coverage.

## What Changes

- Add a spec implementation audit capability that evaluates each main OpenSpec capability against the current codebase and tests.
- Produce a structured implementation gap matrix covering requirement status, implementation evidence, test evidence, risks, and recommended follow-up changes.
- Rank follow-up work so future changes can target the highest-value missing or incomplete capabilities first.
- Avoid changing product runtime behavior; this change is a planning and verification aid only.

## Capabilities

### New Capabilities
- `spec-implementation-audit`: Defines how to audit OpenSpec requirements against implementation and tests, and how to report prioritized gaps.

### Modified Capabilities

None.

## Impact

- Affects OpenSpec planning artifacts and audit documentation.
- Reads existing specs under `openspec/specs/` and relevant implementation/test files.
- Does not require API, database, client, or deployment behavior changes.
- Produces evidence useful for deciding the next implementation-focused OpenSpec change.
