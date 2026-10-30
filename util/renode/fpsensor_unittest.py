#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for util/renode/fpsensor.py."""

import importlib
import sys
import types
from typing import Any
import unittest
from unittest import mock


UNRECOGNIZED_COMMAND = 0xAA


# pylint: disable=missing-function-docstring
class FpSensorTest(unittest.TestCase):
    """Tests for the Renode FpSensor SPI peripheral and registration command."""

    def setUp(self) -> None:
        super().setUp()
        mod_names = (
            "System Antmicro Antmicro.Renode Antmicro.Renode.Core "
            "Antmicro.Renode.Core.Structure Antmicro.Renode.Peripherals "
            "Antmicro.Renode.Peripherals.SPI"
        ).split()
        modules: dict[str, Any] = {n: types.ModuleType(n) for n in mod_names}
        modules["System"].Byte = int
        modules["Antmicro.Renode.Core.Structure"].NullRegistrationPoint = (
            types.SimpleNamespace(Instance=object())
        )
        modules["Antmicro.Renode.Peripherals.SPI"].ISPIPeripheral = object
        patcher = mock.patch.dict(sys.modules, modules)
        patcher.start()
        self.addCleanup(patcher.stop)
        sys.modules.pop("fpsensor", None)
        self.fpsensor = importlib.import_module("fpsensor")

    def _tx(self, sensor: Any, *tx_bytes: int) -> list[int]:
        return [sensor.Transmit(b) for b in tx_bytes]

    def test_fpc_commands(self) -> None:
        fp, fpc = self.fpsensor, self.fpsensor.Fpc
        fpc1025 = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["bloonchipper"])
        self.assertEqual(
            self._tx(fpc1025, fpc.COMMAND_INTERRUPT_STATUS, 0), [0, 0]
        )
        self.assertEqual(
            self._tx(fpc1025, fpc.COMMAND_INTERRUPT_CLEAR, 0), [0, 0]
        )
        self.assertEqual(
            self._tx(fpc1025, fpc.COMMAND_HARDWARE_ID, 0, 0, 0),
            [0, 0x02, 0x1A, 0],
        )
        fpc1145 = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["dartmonkey"])
        self.assertEqual(
            self._tx(fpc1145, fpc.COMMAND_HARDWARE_ID, 0, 0, 0),
            [0, 0x14, 0x0A, 0],
        )

    def test_elan80sg_commands(self) -> None:
        fp, elan = self.fpsensor, self.fpsensor.Elan
        sensor = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["buccaneer"])
        self.assertEqual(
            self._tx(sensor, elan.COMMAND_BYPASS_MODE, 2, 0, 0), [0, 0, 0, 0]
        )
        self.assertEqual(
            self._tx(sensor, elan.COMMAND_HARDWARE_ID_HIGH, 0), [0, 0x4F]
        )
        self.assertEqual(
            self._tx(sensor, elan.COMMAND_HARDWARE_ID_LOW, 0), [0, 0x4F]
        )

    def test_unrecognized_command_and_state_resets(self) -> None:
        fp, fpc = self.fpsensor, self.fpsensor.Fpc
        sensor = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["helipilot"])
        self.assertEqual(self._tx(sensor, UNRECOGNIZED_COMMAND, 0), [0, 0])
        self.assertEqual(
            self._tx(sensor, fpc.COMMAND_HARDWARE_ID, 0), [0, 0x02]
        )
        sensor.Reset()
        self.assertEqual(sensor.Transmit(0), 0)
        self.assertEqual(
            self._tx(sensor, fpc.COMMAND_HARDWARE_ID, 0), [0, 0x02]
        )
        sensor.FinishTransmission()
        self.assertEqual(sensor.Transmit(0), 0)

    def test_register_fp_sensor(self) -> None:
        fp = self.fpsensor
        mock_bus = mock.MagicMock()
        mock_machine = mock.MagicMock()
        mock_machine.__getitem__.return_value = mock_bus
        fp.self = types.SimpleNamespace(Machine=mock_machine)
        fp.mc_RegisterFpSensor("sysbus.spi2", "bloonchipper")
        mock_machine.__getitem__.assert_called_once_with("sysbus.spi2")
        sensor, reg_point = mock_bus.Register.call_args.args
        self.assertIsInstance(sensor, fp.FpSensor)
        self.assertIs(
            reg_point, fp.RenodeStructure.NullRegistrationPoint.Instance
        )
        mock_machine.SetLocalName.assert_called_once_with(sensor, "fpsensor")
        with self.assertRaisesRegex(
            ValueError, "Unsupported board for FpSensor: sanok"
        ):
            fp.mc_RegisterFpSensor("sysbus.spi2", "sanok")


if __name__ == "__main__":
    unittest.main()
