# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Renode SPI peripheral models for fingerprint sensors."""

import collections

import Antmicro.Renode.Core.Structure as RenodeStructure  # pylint: disable=import-error
import Antmicro.Renode.Peripherals.SPI as RenodeSPI  # pylint: disable=import-error
import System  # pylint: disable=import-error


# FPC sensor SPI commands
FPC_CMD_INT_STS = 0x18
FPC_CMD_INT_CLR = 0x1C
FPC_CMD_HW_ID = 0xFC

# Elan sensor SPI commands
ELAN_CMD_BYPASS_MODE = 0x0B
ELAN_READ_REG_HEAD = 0x40
ELAN_REG_HWID_HI = 0x02
ELAN_REG_HWID_LO = 0x04

# Hardware IDs
FPC1025_HWID = 0x021A
FPC1145_HWID = 0x140A
ELAN80SG_HWID = 0x4F4F


def _fpc_cmd_responses(hwid):
    return {
        FPC_CMD_INT_STS: (0x00,),
        FPC_CMD_INT_CLR: (0x00,),
        FPC_CMD_HW_ID: ((hwid >> 8) & 0xFF, hwid & 0xFF),
    }


def _elan_cmd_responses(hwid):
    return {
        ELAN_CMD_BYPASS_MODE: (0x00, 0x00, 0x00),
        ELAN_READ_REG_HEAD | ELAN_REG_HWID_HI: ((hwid >> 8) & 0xFF,),
        ELAN_READ_REG_HEAD | ELAN_REG_HWID_LO: (hwid & 0xFF,),
    }


BOARD_SENSOR_COMMANDS = {
    "bloonchipper": _fpc_cmd_responses(FPC1025_HWID),
    "buccaneer": _elan_cmd_responses(ELAN80SG_HWID),
    "dartmonkey": _fpc_cmd_responses(FPC1145_HWID),
    "helipilot": _fpc_cmd_responses(FPC1025_HWID),
    "nami_fp": _fpc_cmd_responses(FPC1145_HWID),
    "nocturne_fp": _fpc_cmd_responses(FPC1145_HWID),
    "rosalia": _fpc_cmd_responses(FPC1025_HWID),
}


class FpSensor(RenodeSPI.ISPIPeripheral):
    """Command-driven SPI fingerprint sensor peripheral for Renode."""

    def __init__(self, cmd_responses):
        self._cmd_responses = cmd_responses
        self._response = collections.deque()

    def Reset(self):  # pylint: disable=invalid-name
        """Reset peripheral state."""
        self._response.clear()

    def Transmit(self, data):  # pylint: disable=invalid-name
        """Handle a single full-duplex SPI byte transfer."""
        if self._response:
            return System.Byte(self._response.popleft())

        response = self._cmd_responses.get(int(data))
        if response:
            self._response.extend(response)

        return System.Byte(0)

    def FinishTransmission(self):  # pylint: disable=invalid-name
        """Handle end of SPI transaction."""
        self._response.clear()


def mc_RegisterFpSensor(spi_bus, board_name):  # pylint: disable=invalid-name
    """Register an FpSensor peripheral on the given SPI controller."""
    cmd_responses = BOARD_SENSOR_COMMANDS.get(str(board_name))
    if cmd_responses is None:
        raise ValueError("Unsupported board for FpSensor: %s" % board_name)

    # `self` is the Renode Monitor instance injected into the script scope.
    machine = self.Machine  # pylint: disable=undefined-variable
    bus = machine[spi_bus] if isinstance(spi_bus, str) else spi_bus
    sensor = FpSensor(cmd_responses)
    bus.Register(sensor, RenodeStructure.NullRegistrationPoint.Instance)
    machine.SetLocalName(sensor, "fpsensor")
