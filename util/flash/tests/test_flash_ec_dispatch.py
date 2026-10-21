#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for the FLASH_EC_IMPLEMENTATION dispatch in util/flash_ec."""

import os
import pathlib
import signal
import subprocess
import tempfile
from typing import Optional
import unittest


FLASH_EC = pathlib.Path(__file__).resolve().parents[2] / "flash_ec"


def run_flash_ec(
    *args: str, impl: str = "", cwd: Optional[str] = None, **env: str
) -> subprocess.CompletedProcess:
    """Runs util/flash_ec with shflags missing, so the bash flow exits early."""
    return subprocess.run(
        [FLASH_EC, *args],
        cwd=cwd,
        env=dict(
            os.environ,
            SHFLAGS="/nonexistent",
            FLASH_EC_IMPLEMENTATION=impl,
            **env,
        ),
        capture_output=True,
        text=True,
        check=False,
    )


class TestFlashEcDispatch(unittest.TestCase):
    """Tests for choosing between flash_ec.py and the bash flash_ec flow."""

    def test_python_success_skips_bash(self) -> None:
        """Run util/flash/flash_ec.py, not a flash/ package in the cwd."""
        with tempfile.TemporaryDirectory() as tmp:
            shadow = pathlib.Path(tmp, "flash")
            shadow.mkdir()
            for name in ("__init__.py", "flash_ec.py"):
                (shadow / name).write_text(
                    "print('SHADOW')\n", encoding="utf-8"
                )
            result = run_flash_ec("--help", impl="python", cwd=tmp)
        self.assertEqual(result.returncode, 0)
        self.assertIn("usage: flash_ec.py", result.stdout)
        self.assertNotIn("SHADOW", result.stdout)
        self.assertNotIn("/nonexistent", result.stderr)

    def test_python_failure_falls_back_to_bash(self) -> None:
        """A flag flash_ec.py rejects (exit 2) falls back to the bash flow."""
        result = run_flash_ec("--not_a_flash_ec_flag", impl="python")
        self.assertIn("flash_ec.py failed (exit 2)", result.stderr)
        self.assertIn("/nonexistent", result.stderr)
        self.assertEqual(result.returncode, 1)

    def test_python_killed_by_signal_skips_bash(self) -> None:
        """Do not fall back if flash_ec.py dies from a signal."""
        with tempfile.TemporaryDirectory() as tmp:
            # sitecustomize runs at interpreter startup, before flash_ec.py.
            pathlib.Path(tmp, "sitecustomize.py").write_text(
                "import os, signal\nos.kill(os.getpid(), signal.SIGKILL)\n",
                encoding="utf-8",
            )
            result = run_flash_ec("--help", impl="python", PYTHONPATH=tmp)
        self.assertEqual(result.returncode, 128 + signal.SIGKILL)
        self.assertNotIn("/nonexistent", result.stderr)

    def test_default_runs_bash_only(self) -> None:
        """Without the opt-in, flash_ec.py is never run."""
        result = run_flash_ec("--help")
        self.assertNotIn("usage: flash_ec.py", result.stdout)
        self.assertNotIn("flash_ec.py", result.stderr)
        self.assertIn("/nonexistent", result.stderr)
        self.assertEqual(result.returncode, 1)


if __name__ == "__main__":
    unittest.main()
