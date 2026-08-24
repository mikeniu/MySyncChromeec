# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import logging
import os
import time
from typing import Generator

import pytest
import serial

logger = logging.getLogger(__name__)


class TestControlBoard:
    """Interface to interact with the Dagwood test control board shell over serial."""

    def __init__(
        self,
        port: str = "/dev/ttyACM0",
        baudrate: int = 115200,
        timeout: float = 2.0,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser: serial.Serial | None = None

    def open(self) -> None:
        """Open serial connection to test control board."""
        logger.info(
            "Connecting to test control board on %s (%d baud)...",
            self.port,
            self.baudrate,
        )
        self.ser = serial.Serial(
            self.port, baudrate=self.baudrate, timeout=self.timeout
        )
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        # Send newline to sync shell prompt
        self.ser.write(b"\n")
        time.sleep(0.1)
        if self.ser.in_waiting:
            self.ser.read(self.ser.in_waiting)

    def close(self) -> None:
        """Close serial connection."""
        if self.ser and self.ser.is_open:
            self.ser.close()

    def exec_command(self, cmd: str) -> str:
        """Send a command to the control board shell and return response."""
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Test control board serial connection is not open")

        cmd_str = f"{cmd.strip()}\n"
        logger.info("Control board exec: %s", cmd.strip())
        self.ser.write(cmd_str.encode("utf-8"))
        self.ser.flush()
        time.sleep(0.2)
        response = ""
        if self.ser.in_waiting:
            response = self.ser.read(self.ser.in_waiting).decode(
                "utf-8", errors="ignore"
            )
            logger.debug("Control board response: %s", response)
        return response

    def set_tach(self, rpm: int) -> None:
        """Set the fake tachometer RPM output via 'fan set_tach <rpm>'."""
        self.exec_command(f"fan set_tach {rpm}")


@pytest.fixture(scope="function")
def control_board() -> Generator[TestControlBoard, None, None]:
    """Pytest fixture providing connection to the test control board (/dev/ttyACM0)."""
    port = os.environ.get("TEST_CONTROL_PORT", "/dev/ttyACM0")
    cb = TestControlBoard(port=port)
    cb.open()
    try:
        yield cb
    finally:
        try:
            cb.set_tach(0)
        except Exception:
            pass
        cb.close()
