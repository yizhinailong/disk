## 1. Decision Documentation

- [x] 1.1 Create `docs/backend-refactor-decisions.md` with decision records for storage accounting, instant upload accounting, trash quota, private download metadata, share download metadata, JWT ownership, and Redis rate-limit failure policy
- [x] 1.2 For each decision record, label current implementation behavior, accepted target behavior, rationale, rejected alternatives, implementation impact, and follow-up status
- [x] 1.3 Link `docs/backend-discovery.md` as the source of confirmed current behavior and clearly separate discovery facts from target decisions

## 2. Backend Roadmap Sync

- [x] 2.1 Update `docs/TODO.md` Phase 0.1 to mark documentation sync items covered by this decision note as complete or linked to the new document
- [x] 2.2 Update `docs/TODO.md` Phase 0.2 to mark the covered product semantics decisions as complete and leave behavior-changing implementation work open where runtime behavior still differs
- [x] 2.3 Update `docs/TODO.md` Phase 2.1 to record `global-with-exemptions` as the selected JWT strategy while keeping duplicate-filter removal and exact-once tests open for implementation
- [x] 2.4 Update `docs/TODO.md` Phase 2.3 to record fail-open Redis rate-limit behavior as the selected temporary policy while keeping explicit code/test follow-up open
- [x] 2.5 Update the remaining open-questions list in `docs/TODO.md` so resolved questions point to `docs/backend-refactor-decisions.md` and unresolved questions remain open

## 3. Verification

- [x] 3.1 Verify the documentation change does not modify business code, runtime configuration, database schema, or dependency files
- [x] 3.2 Run the relevant OpenSpec validation/status command for `document-backend-refactor-decisions` and resolve any artifact format issues
- [x] 3.3 Review the final docs diff to confirm it records decisions without implying runtime behavior has already changed
