# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import logging
import os
from pathlib import Path
import sys
from typing import Generator

import pytest
import usb.core


DAGWOOD_DIR = Path(__file__).resolve().parents[6] / "dagwood"
if str(DAGWOOD_DIR) not in sys.path:
    sys.path.append(str(DAGWOOD_DIR))

# pylint: disable=import-error, wrong-import-position
import utils


logger = logging.getLogger(__name__)


def pytest_addoption(parser: pytest.Parser) -> None:
    """Register custom CLI options for pytest."""
    parser.addoption(
        "--board-id",
        action="store",
        default=None,
        help="Dagwood board serial number",
    )


def get_board_id(config: pytest.Config) -> str | None:
    """Determine the Dagwood board serial number."""
    board_id = config.getoption("--board-id") or os.environ.get(
        "DAGWOOD_BOARD_ID"
    )
    logger.info("Selected board-id: %s", board_id)
    return board_id


@pytest.fixture(scope="function")
def dagwood_dev(
    pytestconfig: pytest.Config,
) -> Generator[usb.core.Device, None, None]:
    """Pytest fixture providing the Dagwood USB device."""
    board_id = get_board_id(pytestconfig)
    dev = utils.find_usb_device(board_id)
    try:
        yield dev
    finally:
        try:
            utils.dw_fan_set_tach(dev, 0)
        except Exception:
            pass
