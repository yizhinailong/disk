# Self-Contained Backend CTest Closure

**Completed:** 2026-07-18
**Issue:** [#29](https://github.com/yizhinailong/disk/issues/29)

## Outcome

Backend CTest no longer depends on a manually running server or integration-test order. Every ordinary HTTP integration script acquires the backend through `ensure_server()`. The helper borrows an existing server without stopping it, owns and cleans up only a server it starts, releases failed startups, and performs idempotent cleanup.

CTest registrations now use one server-backed helper that preserves `RUN_SERIAL`, timeouts, and the repository-root working directory while rejecting scripts that omit `ensure_server()`. The S3 adapter and isolated S3 application flow remain explicit environment gates and are reported as skipped when their gates are closed.

## Test Inventory Audit

The issue baseline had 20 disabled GoogleTests; one stale share query placeholder had already been removed on main, leaving 19 at implementation time. The remaining 19 inert placeholders and the obsolete unconditional legacy-token migration skip were removed after their executable replacement coverage was recorded in `docs/design/04-系统测试计划.md`. The resulting inventory has zero disabled GoogleTests and zero unconditional GoogleTest skips.

## Verification

- Configure and backend build passed with `linux-debug-clang`.
- The eight originally failing integration entries passed 8/8 from an initially stopped server in 20.46 seconds.
- Full CTest passed all 1,171 enabled non-gated tests; the two closed S3 gates were reported as skipped. Total: 1,173 entries, 0 failures, 157.16 seconds.
- Both S3 gates were then enabled against Moto 5.2.2 at `127.0.0.1:19000`; adapter and application flow passed 2/2 in 8.81 seconds.
- Ports 8080, 18080, and 19000 were clear after verification.

The authoritative lifecycle contract, audit rationale, exact gate variables, commands, and detailed timings are retained in sections 1.5 through 1.7 of the system test plan.
