# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Renode SPI peripheral models for fingerprint sensors.

NOTE: This file is executed inside Renode's embedded IronPython 2.7.12
interpreter via `include $ORIGIN/fpsensor.py`. It must remain compatible with
Python 2.7 syntax, so PEP 484 type comments and `%` string formatting are used
instead of Python 3 function annotations (PEP 3107) and f-strings (PEP 498).
"""

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
BYTE_MASK = 0xFF
HIGH_BYTE_SHIFT = 8


def _fpc_cmd_responses(hardware_id):
    # type: (int) -> dict[int, tuple[int, ...]]
    """Build SPI command-to-response mapping for an FPC fingerprint sensor."""
    return {
        FPC_CMD_INT_STS: (0x00,),
        FPC_CMD_INT_CLR: (0x00,),
        FPC_CMD_HW_ID: (
            (hardware_id >> HIGH_BYTE_SHIFT) & BYTE_MASK,
            hardware_id & BYTE_MASK,
        ),
    }


def _elan_cmd_responses(hardware_id):
    # type: (int) -> dict[int, tuple[int, ...]]
    """Build SPI command-to-response mapping for an Elan fingerprint sensor."""
    return {
        ELAN_CMD_BYPASS_MODE: (0x00, 0x00, 0x00),
        ELAN_READ_REG_HEAD
        | ELAN_REG_HWID_HI: ((hardware_id >> HIGH_BYTE_SHIFT) & BYTE_MASK,),
        ELAN_READ_REG_HEAD | ELAN_REG_HWID_LO: (hardware_id & BYTE_MASK,),
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
            command_responses: Mapping from SPI command byte to a tuple of
                response bytes clocked out on subsequent SPI transfers.
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
            tx_byte: Byte transmitted by the SPI controller on MOSI.

        Returns:
            The response byte transmitted by the sensor on MISO.
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
        spi_bus: Renode SPI bus peripheral or its string path in the machine.
        board_name: Name of the FPMCU board being emulated.

    Raises:
        ValueError: If board_name does not have a configured sensor mapping.
    """
    command_responses = BOARD_SENSOR_COMMANDS.get(str(board_name))
    if command_responses is None:
        raise ValueError("Unsupported board for FpSensor: %s" % board_name)

    # `self` is the Renode Monitor instance injected into the script scope.
    machine = self.Machine  # pylint: disable=undefined-variable
    bus = machine[spi_bus] if isinstance(spi_bus, str) else spi_bus
    sensor = FpSensor(command_responses)
    bus.Register(sensor, RenodeStructure.NullRegistrationPoint.Instance)
    machine.SetLocalName(sensor, "fpsensor")
