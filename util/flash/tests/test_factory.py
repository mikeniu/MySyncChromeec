#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import unittest
from unittest.mock import MagicMock
from util.flash.factory import FlasherFactory
from util.flash.ite_flasher import IteUartFlasher
from util.flash.npcx_flasher import NpcxSpiFlasher
from util.flash.servo_utils import ServoDriver

class TestFactory(unittest.TestCase):
    def setUp(self):
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.chip = None
        self.args.interface = None

    def test_factory_auto_detect_ite(self):
        self.servo.get.return_value = "it8320"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)

    def test_factory_auto_detect_npcx(self):
        self.servo.get.return_value = "npcx9"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, NpcxSpiFlasher)

    def test_factory_override_chip(self):
        self.servo.get.return_value = "unknown"
        self.args.chip = "it8320"
        flasher = FlasherFactory.create(self.servo, self.args)
        self.assertIsInstance(flasher, IteUartFlasher)
        self.servo.get.assert_not_called()

    def test_factory_unsupported_chip(self):
        self.servo.get.return_value = "pic168"
        with self.assertRaises(NotImplementedError):
            FlasherFactory.create(self.servo, self.args)
