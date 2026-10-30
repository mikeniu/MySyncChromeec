# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Fault injection negative tests for EC AIC AP Power Sequencing timings.

Verifies that the Dagwood AP emulator and pytest assertion helpers correctly
detect deliberate timing violations injected into the EC firmware:
- tPCH12: Primary rail drops before RSMRST# assert (min 400 ns)
- tPCH03: RSMRST_PWRGD to RSMRST# deassert delay < 10 ms (1 ms)
- tPLT04: ALL_SYS_PWRGD to PLT_PWROK delay < 1 ms (0 ms)
- tPCH08: SLP_S3# to PLT_PWROK delay < 1 ms (0 ms)
- tPLT05: ALL_SYS_PWRGD to SYS_PWROK delay < 10 ms (0 ms)
- tPCH43: SoC PWRBTN# pulse width < 18 ms (5 ms)
- tCPU22: SLP_S3# assert to PLT_PWROK deassert > 50 ms (60 ms)
"""

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
from test_pwrseq import check_tcpu22
from test_pwrseq import check_tpch03
from test_pwrseq import check_tpch08
from test_pwrseq import check_tpch12
from test_pwrseq import check_tpch43
from test_pwrseq import check_tplt04
from test_pwrseq import check_tplt05
from test_pwrseq import wait_for_dagwood_state


logger = logging.getLogger(__name__)


def test_ap_pwrseq_fault_power_cycle_timings(
    shell: Shell, dagwood_dev: usb.core.Device
) -> None:
    """Verify that tPCH12, tPCH03, tPLT04, tPCH08, and tPLT05 violations are detected."""
    dagwood_utils.dw_ap_set_mode(dagwood_dev, 0)

    # 1. Trigger S0 -> G3 shutdown via EC console 'apshutdown'
    shell.exec_command("apshutdown")
    state, _, gpio_state = wait_for_dagwood_state(dagwood_dev, 0)
    assert state == 0, f"Expected Dagwood AP state 0 (G3), got {state}"
    assert (
        gpio_state & dagwood_utils.AP_SIG_RSMRST
    ), f"Expected RSMRST# asserted in G3 ({gpio_state:#x})"

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info("Fault injected timings after S0->G3 shutdown: %s", timings)

    # Injected fault: PP5000_A dropped before RSMRST# asserted -> timing must be 0 ns (< 400 ns)
    with pytest.raises(AssertionError, match="tPCH12 violation"):
        check_tpch12(timings)

    # 2. Trigger G3 -> S0 power-up via EC power button press
    dagwood_utils.dw_ap_pwrbtn(dagwood_dev, 100)
    state, _, _ = wait_for_dagwood_state(dagwood_dev, 4)
    assert (
        state == 4
    ), f"Expected Dagwood AP state 4 (S0) after power button wake, got {state}"

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info("Fault injected timings after G3->S0 power-up: %s", timings)

    # Injected fault: rsmrst-delay = <1> (1 ms < 10 ms min)
    with pytest.raises(AssertionError, match="tPCH03 violation"):
        check_tpch03(timings)

    # Injected fault: pch-pwrok-delay = <0> (< 1 ms min)
    with pytest.raises(AssertionError, match="tPLT04 violation"):
        check_tplt04(timings)

    with pytest.raises(AssertionError, match="tPCH08 violation"):
        check_tpch08(timings)

    # Injected fault: sys-pwrok-delay = <0> (< 10 ms min)
    with pytest.raises(AssertionError, match="tPLT05 violation"):
        check_tplt05(timings)


def test_ap_pwrseq_fault_pwrbtn_passthrough_timing(
    dagwood_dev: usb.core.Device,
) -> None:
    """Verify that tPCH43 / Note 28 pulse width violation is detected."""
    time.sleep(0.25)
    dagwood_utils.dw_ap_pwrbtn(dagwood_dev, 50)
    time.sleep(0.35)

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info(
        "Fault injected timings after 50 ms power button press: %s", timings
    )

    # Injected fault: fault_pwrbtn_release releases GPIO_PCH_PWRBTN_L after 5 ms (< 18 ms min)
    with pytest.raises(AssertionError, match="tPCH43/Note 28 violation"):
        check_tpch43(timings)


def test_ap_pwrseq_fault_slp_s3_suspend_timing(
    shell: Shell, dagwood_dev: usb.core.Device
) -> None:
    """Verify that tCPU22 S0->S3 suspend delay violation is detected."""
    try:
        # Switch to MANUAL mode, assert SLP_S0# and SLP_S3#, drop ALL_SYS_PWRGD
        dagwood_utils.dw_ap_set_mode(dagwood_dev, 1)
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_SLP_S0, 0
        )
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_SLP_S3, 0
        )
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_ALL_SYS_PWRGD, 0
        )

        deadline = time.monotonic() + 0.5
        _, _, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)
        while (
            gpio_state & dagwood_utils.AP_SIG_SYS_PWROK
        ) and time.monotonic() < deadline:
            time.sleep(0.01)
            _, _, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)

        assert (gpio_state & dagwood_utils.AP_SIG_SYS_PWROK) == 0, (
            "Expected SYS_PWROK deasserted after SLP_S3# assertion "
            f"({gpio_state:#x})"
        )
        assert (gpio_state & dagwood_utils.AP_SIG_PCH_PWROK) == 0, (
            "Expected PCH_PWROK deasserted after SLP_S3# assertion "
            f"({gpio_state:#x})"
        )

        lines = shell.exec_command("powerinfo")
        output = "\n".join(lines)
        logger.info("EC powerinfo in S3:\n%s", output)
        assert (
            "power state 4 = S3" in output
        ), f"Expected EC in S3, got:\n{output}"

        timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
        logger.info(
            "Fault injected timings after SLP_S3# assertion: %s", timings
        )

        # Injected fault: board_ap_power_action_s0_exit sleeps 60 ms (> 50 ms max)
        with pytest.raises(
            AssertionError,
            match="Unexpected SLP_S3# assert to PLT_PWROK deassert delay",
        ):
            check_tcpu22(timings)
    finally:
        # Restore ALL_SYS_PWRGD, SLP_S3_L, SLP_S0_L high and return to AUTO
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_ALL_SYS_PWRGD, 1
        )
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_SLP_S3, 1
        )
        dagwood_utils.dw_ap_set_signal(
            dagwood_dev, dagwood_utils.AP_OUT_SLP_S0, 1
        )
        dagwood_utils.dw_ap_set_mode(dagwood_dev, 0)
        state, _, _ = wait_for_dagwood_state(dagwood_dev, 4)
        assert (
            state == 4
        ), f"Expected return to S0 (4) after restoring AUTO mode, got {state}"
