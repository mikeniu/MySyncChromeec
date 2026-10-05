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
        mod_names = (
            "System",
            "Antmicro",
            "Antmicro.Renode",
            "Antmicro.Renode.Core",
            "Antmicro.Renode.Core.Structure",
            "Antmicro.Renode.Peripherals",
            "Antmicro.Renode.Peripherals.SPI",
        )
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

    def test_fpc_commands(self) -> None:
        fp = self.fpsensor
        fpc1025 = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["bloonchipper"])
        tx = (
            fp.FPC_CMD_INT_STS,
            0,
            fp.FPC_CMD_INT_CLR,
            0,
            fp.FPC_CMD_HW_ID,
            0,
            0,
            0,
        )
        self.assertEqual(
            [fpc1025.Transmit(b) for b in tx],
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1A, 0x00],
        )
        fpc1145 = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["dartmonkey"])
        self.assertEqual(
            [fpc1145.Transmit(b) for b in (fp.FPC_CMD_HW_ID, 0, 0, 0)],
            [0x00, 0x14, 0x0A, 0x00],
        )

    def test_elan80sg_commands(self) -> None:
        fp = self.fpsensor
        sensor = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["buccaneer"])
        tx = (
            fp.ELAN_CMD_BYPASS_MODE,
            2,
            0,
            0,
            fp.ELAN_READ_HWID_HI,
            0,
            fp.ELAN_READ_HWID_LO,
            0,
        )
        self.assertEqual(
            [sensor.Transmit(b) for b in tx],
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x00, 0x4F],
        )

    def test_unrecognized_command_and_state_resets(self) -> None:
        fp = self.fpsensor
        sensor = fp.FpSensor(fp.BOARD_SENSOR_COMMANDS["helipilot"])
        self.assertEqual([sensor.Transmit(b) for b in (0xAA, 0)], [0x00, 0x00])
        self.assertEqual(
            [sensor.Transmit(b) for b in (fp.FPC_CMD_HW_ID, 0)], [0x00, 0x02]
        )
        sensor.Reset()
        self.assertEqual(sensor.Transmit(0x00), 0x00)
        self.assertEqual(
            [sensor.Transmit(b) for b in (fp.FPC_CMD_HW_ID, 0)], [0x00, 0x02]
        )
        sensor.FinishTransmission()
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_register_fp_sensor(self) -> None:
        fp = self.fpsensor
        mock_bus = mock.MagicMock()
        mock_machine = mock.MagicMock()
        mock_machine.__getitem__.return_value = mock_bus
        fp.self = types.SimpleNamespace(Machine=mock_machine)

        fp.mc_RegisterFpSensor(mock_bus, "bloonchipper")
        sensor, reg_point = mock_bus.Register.call_args.args
        self.assertIsInstance(sensor, fp.FpSensor)
        self.assertIs(
            reg_point, fp.RenodeStructure.NullRegistrationPoint.Instance
        )
        mock_machine.SetLocalName.assert_called_once_with(sensor, "fpsensor")
        fp.mc_RegisterFpSensor("sysbus.spip", "helipilot")
        mock_machine.__getitem__.assert_called_once_with("sysbus.spip")
        with self.assertRaisesRegex(
            ValueError, "Unsupported board for FpSensor: sanok"
        ):
            fp.mc_RegisterFpSensor(mock_bus, "sanok")


if __name__ == "__main__":
    unittest.main()
