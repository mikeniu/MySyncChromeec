# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Pytest fixtures and configuration for EC AIC power sequence tests."""

# pylint: disable=import-error, broad-exception-caught
from __future__ import annotations

import logging
from pathlib import Path
import sys
from typing import Generator

import pytest
import usb.core


DAGWOOD_DIR = Path(__file__).resolve().parents[6] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

LIB_DIR = Path(__file__).resolve().parents[2] / "lib"
if str(LIB_DIR) not in sys.path:
    sys.path.append(str(LIB_DIR))


# pylint: disable=import-error, wrong-import-position
import dagwood_utils
import pytest_utils


logger = logging.getLogger(__name__)


def pytest_addoption(parser: pytest.Parser) -> None:
    """Register custom CLI options for pytest."""
    pytest_utils.pytest_addoption(parser)


@pytest.fixture(scope="function")
def dagwood_dev(
    pytestconfig: pytest.Config,
) -> Generator[usb.core.Device, None, None]:
    """Pytest fixture providing the Dagwood USB device."""
    board_id = pytest_utils.get_board_id(pytestconfig)
    dev = dagwood_utils.find_usb_device(board_id)
    try:
        yield dev
    finally:
        pass
