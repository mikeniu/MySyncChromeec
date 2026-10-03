#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for util/renode_ec_launch.py."""

import pathlib
import unittest
from unittest import mock

import renode_ec_launch


class RenodeEcLaunchTest(unittest.TestCase):
    """Tests for Renode command generation in renode_ec_launch."""

    def _run_launch(self, board: str, zephyr: bool, zephyr_bin: str) -> str:
        with (
            mock.patch.object(pathlib.Path, "exists", return_value=True),
            mock.patch("os.chdir"),
            mock.patch("os.makedirs"),
            mock.patch.object(renode_ec_launch, "msg_run") as mock_run,
        ):
            renode_ec_launch.launch(
                board=board,
                enable_write_protect=False,
                zephyr=zephyr,
                zephyr_bin=zephyr_bin,
                ec_project="ec",
                uart="/tmp/renode-uart",
                gdb_port=0,
            )
            cmd = mock_run.call_args.args[0]
            return cmd[cmd.index("--execute") + 1]

    def test_upstream_zephyr_chudow_overrides_addr_and_console(self) -> None:
        execute_str = self._run_launch(
            board="chudow", zephyr=False, zephyr_bin="/path/to/zephyr.bin"
        )
        self.assertIn("$bin_addr=0x10001000;", execute_str)
        self.assertIn("$vector_table_offset=0x10001000;", execute_str)
        self.assertIn("connector Connect sysbus.usart2 term;", execute_str)

    def test_upstream_zephyr_sanok_overrides_addr_only(self) -> None:
        execute_str = self._run_launch(
            board="sanok", zephyr=False, zephyr_bin="/path/to/zephyr.bin"
        )
        self.assertIn("$bin_addr=0x80001000;", execute_str)
        self.assertIn("$vector_table_offset=0x80001000;", execute_str)
        self.assertIn("connector Connect sysbus.uart0 term;", execute_str)

    def test_ec_zephyr_uses_default_addr_and_console(self) -> None:
        execute_str = self._run_launch(
            board="chudow", zephyr=True, zephyr_bin=""
        )
        self.assertNotIn("$bin_addr=", execute_str)
        self.assertNotIn("$vector_table_offset=", execute_str)
        self.assertIn("connector Connect sysbus.usart3 term;", execute_str)


if __name__ == "__main__":
    unittest.main()
