# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Renode SPI peripheral models for fingerprint sensors.

NOTE: Executed by Renode's IronPython 2.7.12 runtime via `include`, so PEP 484
type comments and `%` formatting are used instead of Python 3 syntax.
"""

import collections

import Antmicro.Renode.Core.Structure as RenodeStructure  # pylint: disable=import-error
import Antmicro.Renode.Peripherals.SPI as RenodeSPI  # pylint: disable=import-error
import System  # pylint: disable=import-error


FPC_CMD_INT_STS = 0x18
FPC_CMD_INT_CLR = 0x1C
FPC_CMD_HW_ID = 0xFC
ELAN_CMD_BYPASS_MODE = 0x0B
ELAN_READ_HWID_HI = 0x42
ELAN_READ_HWID_LO = 0x44
FPC1025_HWID = 0x021A
FPC1145_HWID = 0x140A
ELAN80SG_HWID = 0x4F4F
BYTE_MASK = 0xFF
MSB_SHIFT = 8


def _fpc_cmd_responses(hwid):
    # type: (int) -> dict[int, tuple[int, ...]]
    return {
        FPC_CMD_INT_STS: (0x00,),
        FPC_CMD_INT_CLR: (0x00,),
        FPC_CMD_HW_ID: ((hwid >> MSB_SHIFT) & BYTE_MASK, hwid & BYTE_MASK),
    }


def _elan_cmd_responses(hwid):
    # type: (int) -> dict[int, tuple[int, ...]]
    return {
        ELAN_CMD_BYPASS_MODE: (0x00, 0x00, 0x00),
        ELAN_READ_HWID_HI: ((hwid >> MSB_SHIFT) & BYTE_MASK,),
        ELAN_READ_HWID_LO: (hwid & BYTE_MASK,),
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

    def __init__(self, command_responses):
        # type: (dict[int, tuple[int, ...]]) -> None
        """Initialize the peripheral with a command-to-response mapping.

        Args:
            command_responses: Mapping from command byte to response bytes.
        """
        self._command_responses = command_responses
        self._pending_bytes = collections.deque()

    def Reset(self):  # pylint: disable=invalid-name
        # type: () -> None
        """Reset peripheral state."""
        self._pending_bytes.clear()

    def Transmit(self, tx_byte):  # pylint: disable=invalid-name
        # type: (int) -> System.Byte
        """Handle a single full-duplex SPI byte transfer.

        Args:
            tx_byte: The byte transmitted by the SPI controller.

        Returns:
            The byte clocked back to the SPI controller.
        """
        if self._pending_bytes:
            return System.Byte(self._pending_bytes.popleft())

        response_bytes = self._command_responses.get(int(tx_byte))
        if response_bytes:
            self._pending_bytes.extend(response_bytes)
        return System.Byte(0)

    def FinishTransmission(self):  # pylint: disable=invalid-name
        # type: () -> None
        """Handle end of SPI transaction."""
        self._pending_bytes.clear()


def mc_RegisterFpSensor(spi_bus, board_name):  # pylint: disable=invalid-name
    # type: (object, str) -> None
    """Register an FpSensor peripheral on the given SPI controller.

    Args:
        spi_bus: The SPI bus to register the sensor on (name or object).
        board_name: The board identifier to look up command responses for.

    Raises:
        ValueError: If board_name is not supported.
    """
    command_responses = BOARD_SENSOR_COMMANDS.get(str(board_name))
    if command_responses is None:
        # pylint: disable=consider-using-f-string
        raise ValueError("Unsupported board for FpSensor: %s" % board_name)

    machine = self.Machine  # pylint: disable=undefined-variable
    bus = machine[spi_bus] if isinstance(spi_bus, str) else spi_bus
    sensor = FpSensor(command_responses)
    bus.Register(sensor, RenodeStructure.NullRegistrationPoint.Instance)
    machine.SetLocalName(sensor, "fpsensor")
