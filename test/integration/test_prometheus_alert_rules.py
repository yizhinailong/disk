#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Run the repository alert timeline fixture with the official promtool."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path


def main() -> int:
    configured = os.environ.get("PROMTOOL_BIN")
    discovered = shutil.which("promtool") if configured is None else None
    if configured is None and discovered is None:
        print("SKIP: promtool is not available; release validation must provide it")
        return 0

    if configured is not None:
        configured_path = Path(configured).expanduser()
        if not configured_path.is_file() or not os.access(configured_path, os.X_OK):
            print(f"FAIL: PROMTOOL_BIN is not an executable file: {configured}")
            return 1
        promtool = str(configured_path.resolve())
    else:
        promtool = discovered

    repo_root = Path(__file__).resolve().parents[2]
    rules_dir = repo_root / "deploy" / "prometheus"
    completed = subprocess.run(
        [promtool, "test", "rules", "disk-alerts.test.yml"],
        cwd=rules_dir,
        check=False,
    )
    if completed.returncode != 0:
        return completed.returncode

    print("PASS: Prometheus alert thresholds and hold durations are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
