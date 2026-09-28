# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for EC AIC eSPI link initialization and Virtual Wire operation."""

# pylint: disable=import-error, broad-exception-caught
from __future__ import annotations

import logging
from pathlib import Path
import re
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


def get_ec_vw_level(
    shell: Shell, signal_name: str, timeout: float = 2.0
) -> int:
    """Query 'espi_test get_vw <signal_name>' on EC and return 0 or 1."""
    deadline = time.time() + timeout
    last_output = ""
    pattern = re.compile(rf"VW\s+{re.escape(signal_name)}=([01])")

    while time.time() < deadline:
        lines = shell.exec_command(f"espi_test get_vw {signal_name}")
        last_output = "\n".join(lines)
        if match := pattern.search(last_output):
            return int(match.group(1))
        time.sleep(0.1)

    raise AssertionError(
        f"Failed to parse 'VW {signal_name}=<0|1>' from EC output:\n{last_output}"
    )


def wait_for_dagwood_vw(
    dev: usb.core.Device,
    field_name: str,
    expected_val: int,
    timeout: float = 2.0,
) -> dict:
    """Poll Dagwood eSPI status until field_name matches expected_val."""
    deadline = time.time() + timeout
    last_status = {}

    while time.time() < deadline:
        last_status = dagwood_utils.dw_espi_vw_get(dev)
        if last_status.get(field_name) == expected_val:
            return last_status
        time.sleep(0.05)

    assert last_status.get(field_name) == expected_val, (
        f"Dagwood eSPI '{field_name}' mismatch: got {last_status.get(field_name)}, "
        f"expected {expected_val} (full status: {last_status})"
    )
    return last_status


def test_espi_reset_and_init(
    shell: Shell,
    dagwood_dev: usb.core.Device,
) -> None:
    """Verify eSPI reset, link configuration handshake, and Boot Load Done VW."""
    logger.info("Initializing eSPI link from Dagwood controller emulator")
    dagwood_utils.dw_espi_init(dagwood_dev)

    status = wait_for_dagwood_vw(dagwood_dev, "boot_load_done", 1, timeout=3.0)
    logger.info("Dagwood eSPI status after init: %s", status)

    assert status["reset_deasserted"], "ESPI_RST_N should be deasserted (high)"
    assert status["link_ready"], "eSPI Virtual Wire channel should be ready"
    assert (
        status["last_rsp_code"] & 0x0F
    ) == 0x08, f"Expected ACCEPT (0x08) response code, got 0x{status['last_rsp_code']:02x}"
    assert status["dev_id"] & 0xFF in (
        0x00,
        0x01,
    ), f"Expected eSPI Device ID version 0x00 or 0x01, got 0x{status['dev_id']:08x}"
    assert (
        status["gen_cfg"] & (1 << 31)
    ) != 0, f"Expected CRC checking enabled in Gen Cfg (0x08), got 0x{status['gen_cfg']:08x}"
    assert (
        status["vw_cfg"] & 0x03
    ) == 0x03, f"Expected VW Channel Enabled & Ready in VW Cfg (0x20), got 0x{status['vw_cfg']:08x}"
    assert status["boot_load_done"] == 1, "Expected TARGET_BOOT_LOAD_DONE == 1"
    assert (
        status["boot_load_status"] == 1
    ), "Expected TARGET_BOOT_LOAD_STATUS == 1"

    lines = shell.exec_command("espi_test status")
    ec_status_out = "\n".join(lines)
    logger.info("EC espi_test status: %s", ec_status_out)
    assert (
        "ready=1" in ec_status_out
    ), f"Expected EC to report VW channel ready=1, got:\n{ec_status_out}"


@pytest.mark.parametrize(
    "signal_name",
    [
        "slp_s5",
        "slp_s4",
        "slp_s3",
        "sus_stat",
        "pltrst",
        "slp_a",
        "sus_pwrdn_ack",
        "slp_lan",
        "slp_wlan",
    ],
)
def test_espi_vw_controller_to_target(
    shell: Shell,
    dagwood_dev: usb.core.Device,
    signal_name: str,
) -> None:
    """Verify Controller-to-Target Virtual Wires via PUT_VWIRE."""
    for target_level in (1, 0, 1):
        logger.info(
            "Setting Controller->Target VW %s=%d", signal_name, target_level
        )
        dagwood_utils.dw_espi_vw_set(dagwood_dev, signal_name, target_level)
        ec_level = get_ec_vw_level(shell, signal_name)
        assert (
            ec_level == target_level
        ), f"EC reported {signal_name}={ec_level}, expected {target_level}"


def test_espi_vw_warning_handshakes(
    dagwood_dev: usb.core.Device,
) -> None:
    """Verify automatic EC warning/ack Virtual Wire handshakes.

    1. SUS_WARN# (Index 0x41 bit 0) -> SUS_ACK# (Index 0x40 bit 0)
    2. OOB_RST_WARN (Index 0x03 bit 2) -> OOB_RST_ACK (Index 0x04 bit 0)
    3. HOST_RST_WARN (Index 0x07 bit 0) -> HOST_RST_ACK (Index 0x06 bit 3)
    """
    # Ensure PLTRST# is deasserted (1) so Index 0x06/0x07 are active
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "pltrst", 1)

    # 1. SUS_WARN# -> SUS_ACK# (active low: 1 = deasserted, 0 = asserted)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "sus_warn", 1)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "sus_warn", 0)
    wait_for_dagwood_vw(dagwood_dev, "sus_ack_n", 0)

    dagwood_utils.dw_espi_vw_set(dagwood_dev, "sus_warn", 1)
    wait_for_dagwood_vw(dagwood_dev, "sus_ack_n", 1)

    # 2. OOB_RST_WARN -> OOB_RST_ACK (active high: 1 = asserted, 0 = deasserted)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "oob_rst_warn", 0)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "oob_rst_warn", 1)
    wait_for_dagwood_vw(dagwood_dev, "oob_rst_ack", 1)

    dagwood_utils.dw_espi_vw_set(dagwood_dev, "oob_rst_warn", 0)
    wait_for_dagwood_vw(dagwood_dev, "oob_rst_ack", 0)

    # 3. HOST_RST_WARN -> HOST_RST_ACK (active high: 1 = asserted, 0 = deasserted)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "host_rst_warn", 0)
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "host_rst_warn", 1)
    wait_for_dagwood_vw(dagwood_dev, "host_rst_ack", 1)

    dagwood_utils.dw_espi_vw_set(dagwood_dev, "host_rst_warn", 0)
    wait_for_dagwood_vw(dagwood_dev, "host_rst_ack", 0)


def test_espi_vw_target_to_controller(
    shell: Shell,
    dagwood_dev: usb.core.Device,
) -> None:
    """Verify Target-to-Controller Virtual Wires (WAKE#, PME#, SCI#, SMI#) via GET_VWIRE."""
    # Ensure PLTRST# is deasserted (1) so Index 0x06 (SCI#, SMI#) is enabled on all targets
    dagwood_utils.dw_espi_vw_set(dagwood_dev, "pltrst", 1)

    # Test level transitions on WAKE# (Index 0x04 bit 2) and PME# (Index 0x04 bit 3)
    for ec_sig, dw_field in (("wake", "wake_n"), ("pme", "pme_n")):
        for level in (0, 1):
            logger.info("Commanding EC to send VW %s=%d", ec_sig, level)
            shell.exec_command(f"espi_test send_vw {ec_sig} {level}")
            wait_for_dagwood_vw(dagwood_dev, dw_field, level)

    # Test SCI# and SMI# active-low pulses (Index 0x06 bit 0 and bit 1)
    before = dagwood_utils.dw_espi_vw_get(dagwood_dev)
    sci_before = before["sci_pulse_count"]
    smi_before = before["smi_pulse_count"]

    logger.info("Commanding EC to pulse SCI# over Virtual Wire")
    shell.exec_command("espi_test pulse_vw sci")
    after_sci = wait_for_dagwood_vw(dagwood_dev, "sci_n", 1)
    assert after_sci["sci_pulse_count"] > sci_before, (
        f"Expected SCI# pulse count to increment from {sci_before}, "
        f"got {after_sci['sci_pulse_count']}"
    )

    logger.info("Commanding EC to pulse SMI# over Virtual Wire")
    shell.exec_command("espi_test pulse_vw smi")
    after_smi = wait_for_dagwood_vw(dagwood_dev, "smi_n", 1)
    assert after_smi["smi_pulse_count"] > smi_before, (
        f"Expected SMI# pulse count to increment from {smi_before}, "
        f"got {after_smi['smi_pulse_count']}"
    )
