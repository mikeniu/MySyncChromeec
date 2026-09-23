# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common pytest utilities for EC AIC tests."""

# pylint: disable=import-error
from __future__ import annotations

import logging
import os

import pytest


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
    board_id = config.getoption("--board-id", default=None)
    if not board_id and hasattr(config, "twister_harness_config"):
        twister_config = config.twister_harness_config
        if twister_config and twister_config.devices:
            board_id = twister_config.devices[0].id or None
    if not board_id:
        board_id = os.environ.get("DAGWOOD_BOARD_ID")
    logger.info("Selected board-id: %s", board_id)
    return board_id
