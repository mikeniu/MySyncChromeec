# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common library for EC AIC tests."""

from .pytest_utils import get_board_id
from .pytest_utils import pytest_addoption


__all__ = ["get_board_id", "pytest_addoption"]
