# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for EC AIC I2C ports controlled via Dagwood multiplexer."""

# pylint: disable=import-error, broad-exception-caught
from __future__ import annotations

import logging
from pathlib import Path
import sys
import time

import pytest
from twister_harness import Shell
import usb.core


DAGWOOD_DIR = Path(__file__).resolve().parents[6] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

# pylint: disable=import-error, wrong-import-position
import dagwood_utils


logger = logging.getLogger(__name__)

PORT_TEST_CASES = [
    (0, "test_i2c_port0"),
    (1, "test_i2c_port1"),
    (2, "test_i2c_port2"),
    (3, "test_i2c_port3"),
]


def run_i2c_test(
    shell: Shell,
    dagwood_dev: usb.core.Device,
    port: int,
    case_name: str,
) -> None:
    """Switch Dagwood I2C mux A to port and run ztest case.

    1. Switch Dagwood I2C mux A to the target port via USB vendor request.
    2. Wait for the multiplexer and bus to settle.
    3. Run the corresponding ztest case via the Zephyr shell.
    4. Assert that the test passed.
    """
    logger.info("Setting Dagwood I2C mux A to port %d", port)
    dagwood_utils.dw_i2c_mux_a(dagwood_dev, port)

    # Allow mux switch and bus lines to settle
    time.sleep(0.1)

    cmd = f"ztest run-testcase aic_i2c::{case_name}"
    logger.info("Executing: %s", cmd)
    lines = shell.exec_command(cmd)
    output = "\n".join(lines)
    logger.info("Output from '%s':\n%s", cmd, output)

    assert (
        f"PASS - {case_name}" in output
    ), f"Expected 'PASS - {case_name}' in output, got:\n{output}"
    assert (
        f"FAIL - {case_name}" not in output
    ), f"Test case '{case_name}' failed:\n{output}"


@pytest.mark.parametrize(
    "port,case_name",
    PORT_TEST_CASES,
    ids=[name for _, name in PORT_TEST_CASES],
)
def test_i2c_port(
    shell: Shell,
    dagwood_dev: usb.core.Device,
    port: int,
    case_name: str,
) -> None:
    """Verify I2C port by switching Dagwood mux and running ztest case."""
    run_i2c_test(shell, dagwood_dev, port, case_name)
