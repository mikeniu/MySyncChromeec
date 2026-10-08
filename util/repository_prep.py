#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Consistent entry point for repository preparation utilities."""

import pathlib
import runpy
import sys


def main():
    """Run zmake.repository_prep as a script."""
    ec_dir = pathlib.Path(__file__).resolve().parent.parent
    zmake_dir = ec_dir / "zephyr" / "zmake"
    if zmake_dir.is_dir() and str(zmake_dir) not in sys.path:
        sys.path.insert(1, str(zmake_dir))
    target = zmake_dir / "zmake" / "repository_prep.py"
    runpy.run_path(str(target), run_name="__main__")


if __name__ == "__main__":
    main()
