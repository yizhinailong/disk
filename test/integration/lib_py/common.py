# test/integration/lib_py/common.py
# Shared test infrastructure: colors, logging, counters, evidence, summary.
#
# Mirrors lib/common.sh behavior exactly.

from __future__ import annotations

import os
import sys

# ─── Colors ────────────────────────────────────────────────────────────────────

RED = "\033[0;31m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
CYAN = "\033[0;36m"
NC = "\033[0m"

# ─── Test counters ─────────────────────────────────────────────────────────────

tests_passed: int = 0
tests_failed: int = 0
EVIDENCE_DIR: str = os.environ.get("EVIDENCE_DIR", ".sisyphus/evidence")

# ─── Logging ───────────────────────────────────────────────────────────────────


def log_info(msg: str) -> None:
    print(f"{YELLOW}[INFO]{NC} {msg}")


def log_pass(msg: str) -> None:
    global tests_passed
    tests_passed += 1
    print(f"{GREEN}[PASS]{NC} {msg}")


def log_fail(msg: str) -> None:
    global tests_failed
    tests_failed += 1
    print(f"{RED}[FAIL]{NC} {msg}")


def log_step(msg: str) -> None:
    print(f"{CYAN}[STEP]{NC} {msg}")


def log_section(title: str) -> None:
    print()
    print(f"{CYAN}━━━ {title} ━━━{NC}")


# ─── Evidence ──────────────────────────────────────────────────────────────────


def save_evidence(name: str, data: str) -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)
    with open(os.path.join(EVIDENCE_DIR, name), "w") as f:
        f.write(data)
    log_info(f"Evidence saved: {name}")


def save_raw_evidence(name: str, command_output: str) -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)
    with open(os.path.join(EVIDENCE_DIR, name), "w") as f:
        f.write(command_output)
    log_info(f"Evidence saved: {name}")


# ─── Summary ───────────────────────────────────────────────────────────────────


def print_summary() -> None:
    print()
    print("==========================================")
    print("Test Summary")
    print("==========================================")
    print(f"Passed: {GREEN}{tests_passed}{NC}")
    print(f"Failed: {RED}{tests_failed}{NC}")

    if tests_failed == 0:
        print(f"{GREEN}All tests passed!{NC}")
        sys.exit(0)

    print(f"{RED}Some tests failed.{NC}")
    sys.exit(1)
