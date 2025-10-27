# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests OQC."""

import struct
import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

import command
import ec_commands as commands
import oqc


class FakeCommunication:
    """Fake communication class for testing."""

    def __init__(self):
        """Initializes the fake communication."""
        self.vid = 0x1234
        self.pid = 0x5678
        self.serial_number = "TEST"

    def __del__(self):
        """Destructor."""

    def send(self, cmd_bytes) -> int:
        """Sends data."""
        return len(cmd_bytes)

    def wait(self):
        """Waits."""

    def receive(self, _size=-1):
        """Receives data."""
        assert False


def version_response(curr_image) -> bytes:
    """Creates a version response."""
    get_ver = commands.GetVersionCmd1()
    cmd_header = struct.pack(
        command.RESPONSE_HEADER_FMT,
        3,
        0,
        0,
        struct.calcsize(getattr(get_ver, "_response_fmt")),
        0,
    )
    cmd = cmd_header + struct.pack(
        getattr(get_ver, "_response_fmt"),
        bytes(32),
        bytes(32),
        bytes(32),
        curr_image,
        bytes(32),
    )
    cmd = commands.HostCommand.update_checksum(cmd)
    return cmd


def vendor_response(payload) -> bytes:
    """Creates a vendor response."""
    cmd_header = struct.pack(
        command.RESPONSE_HEADER_FMT, 3, 0, 0, len(payload), 0
    )
    cmd = cmd_header + bytes(payload)
    cmd = commands.HostCommand.update_checksum(cmd)
    return cmd


JSON1 = """
{
"checks":
[
  {
    "offset": "0x07",
    "size":   1,
    "operations": [
      {"equal": "0x03"}
    ]
  },
  {
    "offset": "0x0A",
    "size":   1,
    "operations": [
      {"and": "0x03"},
      {"equal": "0x03"}
    ]
  }
]
}
"""

JSON2 = """
{
"checks":
[
  {
    "offset": "0x07",
    "size":   1,
    "operations": [
      {"equal": "0x00"}
    ]
  },
  {
    "offset": "0x0A",
    "size":   1,
    "operations": [
      {"and": "0x03"},
      {"equal": "0x03"}
    ]
  }
]
}
"""


class TestFpVendorCmd(unittest.TestCase):
    """Tests for the FpVendorCmd."""

    @patch("communication.UsbCommunication", FakeCommunication)
    def test_vendor(self):
        """Tests the vendor command."""
        FakeCommunication.receive = MagicMock(
            return_value=vendor_response(list(range(38)))
        )
        vendor_ec = commands.FpVendorCmd0(0)
        ret = vendor_ec.run(FakeCommunication())
        self.assertEqual(ret, 0)
        self.assertEqual(vendor_ec.response.payload, bytes(list(range(38))))

    @patch("communication.UsbCommunication", FakeCommunication)
    def test_oqc(self):
        """Tests the OQC."""
        json_file = MagicMock()
        json_file.name = "test_file.json"
        json_file.read.return_value = JSON1

        FakeCommunication.receive = MagicMock()
        payload = [0xFF] * 54
        results = {
            stage: ("N/A", oqc.OqcTestResult.NO_TEST)
            for stage in oqc.OqcTestStages
        }
        comm = FakeCommunication()

        # not in RW
        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RO),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, comm, results)
        self.assertEqual(ret, oqc.OqcTestError.SW_INTEGRITY_PROBLEM)

        # wrong size
        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RW),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, comm, results)
        self.assertEqual(ret, oqc.OqcTestError.VENDOR_DATA_PROBLEM)

        # wrong checksum
        payload += [0x00] * 10
        payload[0x07] = 0xFF ^ 0x03
        payload[0x0A] = 0xFF ^ 0x0F
        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RW),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, comm, results)
        self.assertEqual(ret, oqc.OqcTestError.WRONG_CHECKSUM)

        # correct checksum
        payload[0x0B] = 0xFF ^ 0x12
        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RW),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, comm, results)
        self.assertEqual(ret, oqc.OqcTestError.SUCCESS)

        # wrong data for configuration file
        payload[0x0A] = 0xFF ^ 0x01
        payload[0x0B] = 0xFF ^ 0x04
        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RW),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, comm, results)
        self.assertEqual(ret, oqc.OqcTestError.CONFIG_CHECK_FAILED)

    @patch("communication.UsbCommunication", FakeCommunication)
    def test_oqc_real_data(self):
        """Tests the OQC with real data."""
        json_file = MagicMock()
        json_file.name = "test_file.json"
        json_file.read.return_value = JSON2

        FakeCommunication.receive = MagicMock()
        payload = [
            0xFA,
            0xFD,
            0xE9,
            0x02,
            0x2F,
            0x97,
            0xFB,
            0xFF,
            0xFE,
            0xFE,
            0xFC,
            0xA4,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFF,
            0xFB,
            0x33,
            0xEA,
            0xFB,
            0xC7,
            0xEE,
            0x9E,
            0xFD,
            0xE9,
            0xFF,
            0xFF,
            0x01,
            0xEE,
            0x06,
            0x1A,
            0xF3,
            0x21,
            0x10,
            0x2E,
            0x36,
            0x30,
            0x57,
            0x4E,
            0x42,
            0x47,
        ]
        results = {
            stage: ("N/A", oqc.OqcTestResult.NO_TEST)
            for stage in oqc.OqcTestStages
        }

        FakeCommunication.receive.side_effect = [
            version_response(commands.ImageType.RW),
            vendor_response(payload),
        ]
        ret = oqc.run_oqc(json_file, FakeCommunication(), results)
        self.assertEqual(ret, oqc.OqcTestError.SUCCESS)


if __name__ == "__main__":
    unittest.main()
