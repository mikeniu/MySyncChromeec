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


class Fpc:  # pylint: disable=too-few-public-methods
    """FPC sensor SPI commands and hardware IDs (see fingerprint_fpc1145.c)."""

    COMMAND_INTERRUPT_STATUS = 0x18
    COMMAND_INTERRUPT_CLEAR = 0x1C
    COMMAND_HARDWARE_ID = 0xFC
    HARDWARE_ID_FPC1025 = (0x02, 0x1A)
    HARDWARE_ID_FPC1145 = (0x14, 0x0A)

    @classmethod
    def command_responses(cls, hardware_id):
        # type: (tuple[int, int]) -> dict[int, tuple[int, ...]]
        """Return the SPI command-to-response mapping for an FPC sensor."""
        return {
            cls.COMMAND_INTERRUPT_STATUS: (0x00,),
            cls.COMMAND_INTERRUPT_CLEAR: (0x00,),
            cls.COMMAND_HARDWARE_ID: hardware_id,
        }


class Elan:  # pylint: disable=too-few-public-methods
    """Elan sensor SPI commands and hardware IDs (see emul_elan_series.c)."""

    COMMAND_BYPASS_MODE = 0x0B
    COMMAND_HARDWARE_ID_HIGH = 0x42
    COMMAND_HARDWARE_ID_LOW = 0x44
    HARDWARE_ID_ELAN80SG = (0x4F, 0x4F)

    @classmethod
    def command_responses(cls, hardware_id):
        # type: (tuple[int, int]) -> dict[int, tuple[int, ...]]
        """Return the SPI command-to-response mapping for an Elan sensor."""
        hardware_id_high, hardware_id_low = hardware_id
        return {
            cls.COMMAND_BYPASS_MODE: (0x00, 0x00, 0x00),
            cls.COMMAND_HARDWARE_ID_HIGH: (hardware_id_high,),
            cls.COMMAND_HARDWARE_ID_LOW: (hardware_id_low,),
        }


BOARD_SENSOR_COMMANDS = {
    "bloonchipper": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1025),
    "buccaneer": Elan.command_responses(Elan.HARDWARE_ID_ELAN80SG),
    "dartmonkey": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1145),
    "helipilot": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1025),
    "nami_fp": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1145),
    "nocturne_fp": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1145),
    "rosalia": Fpc.command_responses(Fpc.HARDWARE_ID_FPC1025),
}


class FpSensor(RenodeSPI.ISPIPeripheral):
    """Command-driven SPI fingerprint sensor peripheral for Renode."""

    def __init__(self, command_responses):
        # type: (dict[int, tuple[int, ...]]) -> None
        """Initialize the peripheral with a command-to-response mapping."""
        self._responses = command_responses
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

        self._pending_bytes.extend(self._responses.get(int(tx_byte), ()))
        return System.Byte(0)

    def FinishTransmission(self):  # pylint: disable=invalid-name
        # type: () -> None
        """Handle end of SPI transaction."""
        self._pending_bytes.clear()


def mc_RegisterFpSensor(spi_bus, board_name):  # pylint: disable=invalid-name
    # type: (str, str) -> None
    """Register an FpSensor peripheral on the given SPI controller.

    Args:
        spi_bus: Name of the SPI bus peripheral in the machine.
        board_name: The board identifier to look up command responses for.

    Raises:
        ValueError: If board_name is not supported.
    """
    command_responses = BOARD_SENSOR_COMMANDS.get(board_name)
    if command_responses is None:
        # pylint: disable=consider-using-f-string
        raise ValueError("Unsupported board for FpSensor: %s" % board_name)

    machine = self.Machine  # pylint: disable=undefined-variable
    sensor = FpSensor(command_responses)
    machine[spi_bus].Register(
        sensor, RenodeStructure.NullRegistrationPoint.Instance
    )
    machine.SetLocalName(sensor, "fpsensor")
