#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for test/run_device_tests.py."""

import unittest
from unittest import mock

import run_device_tests


# pylint: disable=missing-function-docstring
class RunDeviceTestsTest(unittest.TestCase):
    """Tests for run_device_tests."""

    def test_single_check_failed_regex_matches_failures(self) -> None:
        regex = run_device_tests.SINGLE_CHECK_FAILED_REGEX
        self.assertIsNotNone(
            regex.match("test/foo.c:42: ASSERTION failed: x == y")
        )
        self.assertIsNotNone(
            regex.match('assertion "x == y" failed: file "test/foo.c", line 42')
        )
        self.assertIsNotNone(
            regex.match("    FAIL - test_foo in 0.001 seconds")
        )

    def test_single_check_failed_regex_ignores_driver_errors(self) -> None:
        regex = run_device_tests.SINGLE_CHECK_FAILED_REGEX
        self.assertIsNone(
            regex.match(
                "[0.012345 egis630_pal: SPI PAL transaction failed: -116]"
            )
        )
        self.assertIsNone(regex.match("Pass: all checks succeeded"))

    def test_hardware_fp_sensor_sel_default_sensor_success_power_cycles(
        self,
    ) -> None:
        platform = run_device_tests.Hardware()
        board = run_device_tests.BLOONCHIPPER_CONFIG
        with (
            mock.patch(
                "subprocess.run",
                return_value=mock.Mock(returncode=0),
            ) as mock_run,
            mock.patch.object(run_device_tests, "power_cycle") as mock_power,
        ):
            self.assertTrue(platform.fp_sensor_sel(board))
            mock_run.assert_called_once_with(
                ["dut-control", f"fp_sensor_sel:{board.sensor_type.value}"],
                check=False,
            )
            mock_power.assert_called_once_with(platform, board)

    def test_hardware_fp_sensor_sel_explicit_sensor_failure_skips_power_cycle(
        self,
    ) -> None:
        platform = run_device_tests.Hardware()
        board = run_device_tests.BLOONCHIPPER_CONFIG
        sensor = run_device_tests.FPSensorType.ELAN
        with (
            mock.patch(
                "subprocess.run",
                return_value=mock.Mock(returncode=1),
            ) as mock_run,
            mock.patch.object(run_device_tests, "power_cycle") as mock_power,
        ):
            self.assertFalse(platform.fp_sensor_sel(board, sensor_type=sensor))
            mock_run.assert_called_once_with(
                ["dut-control", f"fp_sensor_sel:{sensor.value}"],
                check=False,
            )
            mock_power.assert_not_called()

    def test_renode_fp_sensor_sel_returns_true_without_dut_control(
        self,
    ) -> None:
        with (
            run_device_tests.Renode() as platform,
            mock.patch("subprocess.run") as mock_run,
        ):
            self.assertTrue(
                platform.fp_sensor_sel(run_device_tests.BLOONCHIPPER_CONFIG)
            )
            mock_run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
