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


class FpSensorTest(unittest.TestCase):
    """Tests for the Renode FpSensor SPI peripheral and registration command."""

    def setUp(self) -> None:
        super().setUp()
        modules: dict[str, Any] = {
            name: types.ModuleType(name)
            for name in (
                "System",
                "Antmicro",
                "Antmicro.Renode",
                "Antmicro.Renode.Core",
                "Antmicro.Renode.Core.Structure",
                "Antmicro.Renode.Peripherals",
                "Antmicro.Renode.Peripherals.SPI",
            )
        }
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

    def test_fpc_commands(self) -> None:
        fpc1025 = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["bloonchipper"]
        )
        tx_seq = (
            self.fpsensor.FPC_CMD_INT_STS,
            0,
            self.fpsensor.FPC_CMD_INT_CLR,
            0,
            self.fpsensor.FPC_CMD_HW_ID,
            0,
            0,
            0,
        )
        self.assertEqual(
            [fpc1025.Transmit(b) for b in tx_seq],
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1A, 0x00],
        )
        fpc1145 = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["dartmonkey"]
        )
        tx_1145 = (self.fpsensor.FPC_CMD_HW_ID, 0, 0, 0)
        self.assertEqual(
            [fpc1145.Transmit(b) for b in tx_1145], [0x00, 0x14, 0x0A, 0x00]
        )

    def test_elan80sg_commands(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["buccaneer"]
        )
        tx_seq = (
            self.fpsensor.ELAN_CMD_BYPASS_MODE,
            0x02,
            0,
            0,
            self.fpsensor.ELAN_READ_HWID_HI,
            0,
            self.fpsensor.ELAN_READ_HWID_LO,
            0,
        )
        self.assertEqual(
            [sensor.Transmit(b) for b in tx_seq],
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x00, 0x4F],
        )

    def test_unrecognized_command_and_state_resets(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["helipilot"]
        )
        self.assertEqual([sensor.Transmit(b) for b in (0xAA, 0)], [0x00, 0x00])
        self.assertEqual(
            [sensor.Transmit(b) for b in (self.fpsensor.FPC_CMD_HW_ID, 0)],
            [0x00, 0x02],
        )
        sensor.Reset()
        self.assertEqual(sensor.Transmit(0x00), 0x00)
        self.assertEqual(
            [sensor.Transmit(b) for b in (self.fpsensor.FPC_CMD_HW_ID, 0)],
            [0x00, 0x02],
        )
        sensor.FinishTransmission()
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_register_fp_sensor(self) -> None:
        mock_bus = mock.MagicMock()
        mock_machine = mock.MagicMock()
        mock_machine.__getitem__.return_value = mock_bus
        self.fpsensor.self = types.SimpleNamespace(Machine=mock_machine)

        self.fpsensor.mc_RegisterFpSensor(mock_bus, "bloonchipper")
        registered_sensor, reg_point = mock_bus.Register.call_args.args
        self.assertIsInstance(registered_sensor, self.fpsensor.FpSensor)
        self.assertIs(
            reg_point,
            self.fpsensor.RenodeStructure.NullRegistrationPoint.Instance,
        )
        mock_machine.SetLocalName.assert_called_once_with(
            registered_sensor, "fpsensor"
        )
        self.fpsensor.mc_RegisterFpSensor("sysbus.spip", "helipilot")
        mock_machine.__getitem__.assert_called_once_with("sysbus.spip")
        with self.assertRaisesRegex(
            ValueError, "Unsupported board for FpSensor: sanok"
        ):
            self.fpsensor.mc_RegisterFpSensor(mock_bus, "sanok")


if __name__ == "__main__":
    unittest.main()
