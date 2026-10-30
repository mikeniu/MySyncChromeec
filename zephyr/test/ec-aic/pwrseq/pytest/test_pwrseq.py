# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for EC AIC AP Power Sequencing via pure GPIO with Dagwood AP emulation."""

# pylint: disable=import-error, broad-exception-caught
from __future__ import annotations

import logging
from pathlib import Path
import sys
import time

from twister_harness import Shell
import usb.core


DAGWOOD_DIR = Path(__file__).resolve().parents[6] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

# pylint: disable=import-error, wrong-import-position
import dagwood_utils


logger = logging.getLogger(__name__)


def wait_for_dagwood_state(
    dagwood_dev: usb.core.Device, target_state: int, timeout_s: float = 2.0
) -> tuple[int, int, int]:
    """Poll Dagwood AP status until target_state is reached or timeout expires."""
    deadline = time.monotonic() + timeout_s
    state, mode, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)
    while state != target_state and time.monotonic() < deadline:
        time.sleep(0.02)
        state, mode, gpio_state = dagwood_utils.dw_ap_get_status(dagwood_dev)
    return state, mode, gpio_state


def check_tpch12(timings: dict[str, int]) -> None:
    """Validate tPCH12: RSMRST# must assert >= 400 ns prior to primary rail drop."""
    assert timings["rsmrst_assert_to_en_s5_deassert_ns"] >= 400, (
        "tPCH12 violation: RSMRST# asserted only "
        f"{timings['rsmrst_assert_to_en_s5_deassert_ns']} ns before "
        "primary rail drop (min 400 ns)"
    )


def check_tpch03(timings: dict[str, int]) -> None:
    """Validate tPCH03: RSMRST_PWRGD high to RSMRST# de-assertion >= 10 ms."""
    assert timings["rsmrst_pwrgd_to_rsmrst_deassert_us"] >= 9500, (
        "tPCH03 violation: RSMRST_PWRGD to RSMRST# deassertion was "
        f"{timings['rsmrst_pwrgd_to_rsmrst_deassert_us']} us (min 10 ms)"
    )


def check_tplt04(timings: dict[str, int]) -> None:
    """Validate tPLT04: ALL_SYS_PWRGD assertion to PLT_PWROK assertion >= 1 ms."""
    assert timings["all_sys_pwrgd_to_plt_pwrok_us"] >= 1000, (
        "tPLT04 violation: ALL_SYS_PWRGD to PLT_PWROK was "
        f"{timings['all_sys_pwrgd_to_plt_pwrok_us']} us (min 1 ms)"
    )


def check_tpch08(timings: dict[str, int]) -> None:
    """Validate tPCH08: SLP_S3# de-assertion to PLT_PWROK assertion >= 1 ms."""
    assert timings["slp_s3_to_plt_pwrok_us"] >= 1000, (
        "tPCH08 violation: SLP_S3# deassertion to PLT_PWROK was "
        f"{timings['slp_s3_to_plt_pwrok_us']} us (min 1 ms)"
    )


def check_tplt05(timings: dict[str, int]) -> None:
    """Validate tPLT05: ALL_SYS_PWRGD to SYS_PWROK >= sys-pwrok-delay (10 ms)."""
    assert timings["all_sys_pwrgd_to_sys_pwrok_us"] >= 9500, (
        "tPLT05 violation: ALL_SYS_PWRGD to SYS_PWROK was "
        f"{timings['all_sys_pwrgd_to_sys_pwrok_us']} us "
        "(expected >= 10 ms sys-pwrok-delay)"
    )
    assert timings["plt_pwrok_to_sys_pwrok_us"] >= 0


def check_tpch43(timings: dict[str, int]) -> None:
    """Validate tPCH43 / Note 28: SoC PWRBTN# pulse width >= 18 ms."""
    assert timings["pwrbtn_pulse_width_us"] >= 18000, (
        "tPCH43/Note 28 violation: SoC PWRBTN# pulse width was "
        f"{timings['pwrbtn_pulse_width_us']} us (min 18 ms)"
    )


def check_tcpu22(timings: dict[str, int]) -> None:
    """Validate tCPU22 / tPCH29: PWROK deasserts on S0->S3 suspend."""
    assert 0 < timings["slp_s3_assert_to_plt_pwrok_deassert_us"] < 50000, (
        "Unexpected SLP_S3# assert to PLT_PWROK deassert delay: "
        f"{timings['slp_s3_assert_to_plt_pwrok_deassert_us']} us"
    )


def test_ap_pwrseq_basic(shell: Shell, dagwood_dev: usb.core.Device) -> None:
    """Basic test to verify EC shell responds and Dagwood AP status is read."""
    dagwood_utils.dw_ap_set_mode(dagwood_dev, 0)
    state, mode, gpio_state = wait_for_dagwood_state(dagwood_dev, 4)
    logger.info(
        "Dagwood AP status: state=%d, mode=%d, gpios=%#x",
        state,
        mode,
        gpio_state,
    )

    lines = shell.exec_command("version")
    logger.info("EC version response:\n%s", "\n".join(lines))

    lines = shell.exec_command("powerinfo")
    output = "\n".join(lines)
    logger.info("EC powerinfo response:\n%s", output)
    assert "power state 7 = S0" in output, f"Expected EC in S0, got:\n{output}"

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


def test_ap_pwrseq_power_cycle_timings(
    shell: Shell, dagwood_dev: usb.core.Device
) -> None:
    """Validate S0->G3 shutdown and G3->S0 power-up sequence timings."""
    dagwood_utils.dw_ap_set_mode(dagwood_dev, 0)

    # 1. Trigger S0 -> G3 shutdown via EC console 'apshutdown'
    shell.exec_command("apshutdown")
    state, _, gpio_state = wait_for_dagwood_state(dagwood_dev, 0)
    assert state == 0, f"Expected Dagwood AP state 0 (G3), got {state}"
    assert (
        gpio_state & dagwood_utils.AP_SIG_RSMRST
    ), f"Expected RSMRST# asserted in G3 ({gpio_state:#x})"
    assert (
        gpio_state & dagwood_utils.AP_SIG_SYS_PWROK
    ) == 0, f"Expected SYS_PWROK deasserted in G3 ({gpio_state:#x})"

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info("Timings after S0->G3 shutdown: %s", timings)
    check_tpch12(timings)

    # 2. Trigger G3 -> S0 power-up via EC power button press
    dagwood_utils.dw_ap_pwrbtn(dagwood_dev, 100)
    state, _, gpio_state = wait_for_dagwood_state(dagwood_dev, 4)
    assert (
        state == 4
    ), f"Expected Dagwood AP state 4 (S0) after power button wake, got {state}"

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info("Timings after G3->S0 power-up: %s", timings)

    check_tpch03(timings)
    check_tplt04(timings)
    check_tpch08(timings)
    check_tplt05(timings)


def test_ap_pwrseq_pwrbtn_passthrough_timing(
    dagwood_dev: usb.core.Device,
) -> None:
    """Validate tPCH43 / Note 28: SoC PWRBTN# pulse width >= 18 ms."""
    time.sleep(0.25)
    dagwood_utils.dw_ap_pwrbtn(dagwood_dev, 50)
    time.sleep(0.35)

    timings = dagwood_utils.dw_ap_get_timings(dagwood_dev)
    logger.info("Timings after 50 ms power button press: %s", timings)
    check_tpch43(timings)


def test_ap_pwrseq_slp_s3_suspend_timing(
    shell: Shell, dagwood_dev: usb.core.Device
) -> None:
    """Validate tCPU22 / tPCH29: PWROK deasserts on S0->S3 suspend."""
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
        logger.info("Timings after SLP_S3# assertion: %s", timings)
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
