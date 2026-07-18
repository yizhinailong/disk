#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["httpx"]
# ///

"""Regression coverage for the shared integration-server ownership contract."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))

import lib_py.auth as auth


class FakeProcess:
    def __init__(self, *, returncode: int | None = None, timeout_on_terminate: bool = False):
        self.returncode = returncode
        self.timeout_on_terminate = timeout_on_terminate
        self.terminate_calls = 0
        self.kill_calls = 0
        self.wait_calls = 0

    def poll(self) -> int | None:
        return self.returncode

    def terminate(self) -> None:
        self.terminate_calls += 1

    def kill(self) -> None:
        self.kill_calls += 1
        self.returncode = -9

    def wait(self, timeout: float | None = None) -> int:
        self.wait_calls += 1
        if self.timeout_on_terminate and self.kill_calls == 0:
            raise subprocess.TimeoutExpired("disk", timeout)
        if self.returncode is None:
            self.returncode = -15
        return self.returncode


class ServerLifecycleTest(unittest.TestCase):
    def setUp(self) -> None:
        auth.cleanup()
        self.temp_dir = tempfile.TemporaryDirectory(prefix="disk-server-lifecycle-")
        self.addCleanup(self.temp_dir.cleanup)
        self.log_path = os.path.join(self.temp_dir.name, "server.log")

    def tearDown(self) -> None:
        auth.cleanup()

    def ensure_with(self, process: FakeProcess, ready_states: list[bool]) -> None:
        with (
            mock.patch.object(auth, "server_ready", side_effect=ready_states),
            mock.patch.object(auth.subprocess, "Popen", return_value=process),
            mock.patch.dict(os.environ, {"SERVER_LOG": self.log_path}),
        ):
            auth.ensure_server(server_bin=sys.executable)

    def test_ready_external_server_is_borrowed(self) -> None:
        with (
            mock.patch.object(auth, "server_ready", return_value=True),
            mock.patch.object(auth.subprocess, "Popen") as popen,
        ):
            auth.ensure_server(server_bin=sys.executable)
            auth.cleanup()

        popen.assert_not_called()
        self.assertFalse(auth._managed_server)
        self.assertIsNone(auth._server_process)

    def test_started_server_is_stopped_and_cleanup_is_idempotent(self) -> None:
        process = FakeProcess()
        self.ensure_with(process, [False, True])
        log_handle = auth._server_log_handle

        self.assertTrue(auth._managed_server)
        self.assertIs(auth._server_process, process)
        self.assertIsNotNone(log_handle)

        auth.cleanup()
        auth.cleanup()

        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 0)
        self.assertTrue(log_handle.closed)
        self.assertFalse(auth._managed_server)
        self.assertIsNone(auth._server_process)

    def test_cleanup_kills_server_that_ignores_terminate(self) -> None:
        process = FakeProcess(timeout_on_terminate=True)
        self.ensure_with(process, [False, True])

        auth.cleanup()

        self.assertEqual(process.terminate_calls, 1)
        self.assertEqual(process.kill_calls, 1)
        self.assertEqual(process.wait_calls, 2)

    def test_startup_failure_releases_process_and_log(self) -> None:
        process = FakeProcess(returncode=7)
        with (
            mock.patch.object(auth, "server_ready", side_effect=[False, False]),
            mock.patch.object(auth.subprocess, "Popen", return_value=process),
            mock.patch.dict(os.environ, {"SERVER_LOG": self.log_path}),
            self.assertRaises(SystemExit),
        ):
            auth.ensure_server(server_bin=sys.executable)

        self.assertEqual(process.terminate_calls, 1)
        self.assertFalse(auth._managed_server)
        self.assertIsNone(auth._server_process)
        self.assertIsNone(auth._server_log_handle)

    def test_spawn_failure_releases_log(self) -> None:
        log_file = mock.mock_open()
        with (
            mock.patch.object(auth, "server_ready", return_value=False),
            mock.patch.object(auth.subprocess, "Popen", side_effect=OSError("spawn failed")),
            mock.patch("builtins.open", log_file),
            self.assertRaisesRegex(OSError, "spawn failed"),
        ):
            auth.ensure_server(server_bin=sys.executable)

        log_file().close.assert_called_once_with()
        self.assertFalse(auth._managed_server)
        self.assertIsNone(auth._server_process)
        self.assertIsNone(auth._server_log_handle)


if __name__ == "__main__":
    unittest.main()
