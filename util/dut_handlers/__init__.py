# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""DUT handler package for lease_and_test_ec."""

import argparse
from typing import Any, Dict

from .base import DutHandler
from .base import DutOsType
from .cros import CrosHandler


def create_dut_handler(
    details: Dict[str, Any], args: argparse.Namespace, ec_dir: str
) -> DutHandler:
    """Instantiate the appropriate target handler for the DUT OS type."""
    os_type = details.get("os_type")
    if os_type == DutOsType.ANDROID:
        raise NotImplementedError(
            f"Leased DUT has OS type '{os_type}', but Android is not supported yet."
        )
    return CrosHandler(details, args, ec_dir)


__all__ = [
    "DutHandler",
    "DutOsType",
    "CrosHandler",
    "create_dut_handler",
]
