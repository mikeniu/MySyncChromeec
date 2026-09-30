# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for EC AIC AP Power Sequencing via pure GPIO with Dagwood AP emulation."""

# pylint: disable=import-error, broad-exception-caught
from __future__ import annotations

import logging
from pathlib import Path
import sys

from twister_harness import Shell
import usb.core


DAGWOOD_DIR = Path(__file__).resolve().parents[6] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

# pylint: disable=import-error, wrong-import-position
import dagwood_utils


logger = logging.getLogger(__name__)


def test_ap_pwrseq_basic(shell: Shell, dagwood_dev: usb.core.Device) -> None:
    """Basic test to verify EC shell responds and Dagwood AP status is read."""
    # Read status from Dagwood emulator
    state, mode, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)
    logger.info(
        "Dagwood AP status: state=%d, mode=%d, gpios=%#x",
        state,
        mode,
        gpio_state,
    )

    # Query EC powerinfo and version via shell fixture
    lines = shell.exec_command("version")
    logger.info("EC version response:\n%s", "\n".join(lines))

    lines = shell.exec_command("powerinfo")
    output = "\n".join(lines)
    logger.info("EC powerinfo response:\n%s", output)
    assert "power state 7 = S0" in output, f"Expected EC in S0, got:\n{output}"

    # Re-read Dagwood status
    state, mode, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)
    logger.info(
        "Dagwood AP status after powerinfo: state=%d, mode=%d, gpios=%#x",
        state,
        mode,
        gpio_state,
    )
    assert state == 4, f"Expected Dagwood AP state 4 (S0), got {state}"
    assert (
        gpio_state & dagwood_utils.AP_SIG_RSMRST_PWRGD
    ), f"Expected RSMRST_PWRGD asserted in gpio_state ({gpio_state:#x})"
    assert (
        gpio_state & dagwood_utils.AP_SIG_PCH_PWROK
    ), f"Expected PCH_PWROK asserted in gpio_state ({gpio_state:#x})"
    assert (
        gpio_state & dagwood_utils.AP_SIG_SYS_PWROK
    ), f"Expected SYS_PWROK asserted in gpio_state ({gpio_state:#x})"
