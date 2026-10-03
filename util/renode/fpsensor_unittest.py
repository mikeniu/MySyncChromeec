#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for util/renode/fpsensor.py."""

import importlib
import sys
import types
import unittest
from unittest import mock


class FpSensorTest(unittest.TestCase):
    """Tests for the Renode FpSensor SPI peripheral and registration command."""

    def setUp(self) -> None:
        super().setUp()
        system_module = types.ModuleType("System")
        system_module.Byte = int  # type: ignore[attr-defined]

        structure_module = types.ModuleType("Antmicro.Renode.Core.Structure")
        null_registration = types.SimpleNamespace(Instance=object())
        structure_module.NullRegistrationPoint = (  # type: ignore[attr-defined]
            null_registration
        )

        spi_module = types.ModuleType("Antmicro.Renode.Peripherals.SPI")
        spi_module.ISPIPeripheral = object  # type: ignore[attr-defined]

        patcher = mock.patch.dict(
            sys.modules,
            {
                "System": system_module,
                "Antmicro": types.ModuleType("Antmicro"),
                "Antmicro.Renode": types.ModuleType("Antmicro.Renode"),
                "Antmicro.Renode.Core": types.ModuleType(
                    "Antmicro.Renode.Core"
                ),
                "Antmicro.Renode.Core.Structure": structure_module,
                "Antmicro.Renode.Peripherals": types.ModuleType(
                    "Antmicro.Renode.Peripherals"
                ),
                "Antmicro.Renode.Peripherals.SPI": spi_module,
            },
        )
        patcher.start()
        self.addCleanup(patcher.stop)

        sys.modules.pop("fpsensor", None)
        self.fpsensor = importlib.import_module("fpsensor")

    def test_fpc1025_commands(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["bloonchipper"]
        )

        # Interrupt status read: command byte returns 0x00, next byte is 0x00.
        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_INT_STS), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

        # Interrupt clear read: command byte returns 0x00, next byte is 0x00.
        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_INT_CLR), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

        # Hardware ID read (0x021A): command byte returns 0x00, followed by MSB
        # (0x02) and LSB (0x1A), then 0x00 once the response queue is empty.
        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_HW_ID), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x02)
        self.assertEqual(sensor.Transmit(0x00), 0x1A)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_fpc1145_commands(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["dartmonkey"]
        )

        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_HW_ID), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x14)
        self.assertEqual(sensor.Transmit(0x00), 0x0A)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_elan80sg_commands(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["buccaneer"]
        )

        # Bypass mode command (0x0B) followed by 3 clocked bytes returns 0x00.
        self.assertEqual(
            sensor.Transmit(self.fpsensor.ELAN_CMD_BYPASS_MODE), 0x00
        )
        self.assertEqual(sensor.Transmit(0x02), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

        # Hardware ID high and low register reads (0x4F4F).
        read_hwid_hi = (
            self.fpsensor.ELAN_READ_REG_HEAD | self.fpsensor.ELAN_REG_HWID_HI
        )
        self.assertEqual(sensor.Transmit(read_hwid_hi), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x4F)

        read_hwid_lo = (
            self.fpsensor.ELAN_READ_REG_HEAD | self.fpsensor.ELAN_REG_HWID_LO
        )
        self.assertEqual(sensor.Transmit(read_hwid_lo), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x4F)

    def test_unrecognized_command_returns_zero(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["helipilot"]
        )
        unrecognized_cmd = 0xAA

        self.assertEqual(sensor.Transmit(unrecognized_cmd), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_reset_clears_pending_response(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["bloonchipper"]
        )

        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_HW_ID), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x02)
        sensor.Reset()
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_finish_transmission_clears_pending_response(self) -> None:
        sensor = self.fpsensor.FpSensor(
            self.fpsensor.BOARD_SENSOR_COMMANDS["bloonchipper"]
        )

        self.assertEqual(sensor.Transmit(self.fpsensor.FPC_CMD_HW_ID), 0x00)
        self.assertEqual(sensor.Transmit(0x00), 0x02)
        sensor.FinishTransmission()
        self.assertEqual(sensor.Transmit(0x00), 0x00)

    def test_register_fp_sensor_with_peripheral_bus(self) -> None:
        mock_bus = mock.MagicMock()
        mock_machine = mock.MagicMock()
        self.fpsensor.self = types.SimpleNamespace(Machine=mock_machine)

        self.fpsensor.mc_RegisterFpSensor(mock_bus, "bloonchipper")

        mock_bus.Register.assert_called_once()
        registered_sensor, reg_point = mock_bus.Register.call_args.args
        self.assertIsInstance(registered_sensor, self.fpsensor.FpSensor)
        self.assertIs(
            reg_point,
            self.fpsensor.RenodeStructure.NullRegistrationPoint.Instance,
        )
        mock_machine.SetLocalName.assert_called_once_with(
            registered_sensor, "fpsensor"
        )

    def test_register_fp_sensor_with_string_bus_name(self) -> None:
        mock_bus = mock.MagicMock()
        mock_machine = mock.MagicMock()
        mock_machine.__getitem__.return_value = mock_bus
        self.fpsensor.self = types.SimpleNamespace(Machine=mock_machine)

        self.fpsensor.mc_RegisterFpSensor("sysbus.spip", "helipilot")

        mock_machine.__getitem__.assert_called_once_with("sysbus.spip")
        mock_bus.Register.assert_called_once()

    def test_register_fp_sensor_unsupported_board_raises(self) -> None:
        with self.assertRaisesRegex(
            ValueError, "Unsupported board for FpSensor: sanok"
        ):
            self.fpsensor.mc_RegisterFpSensor(mock.MagicMock(), "sanok")


if __name__ == "__main__":
    unittest.main()
