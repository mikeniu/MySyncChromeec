#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper script to run Dagwood device tests inside the Docker container."""

import os
from pathlib import Path
import sys


SCRIPT_DIR = Path(__file__).resolve().parent


def main():
    """Forward all arguments to ./dagwood inside the Docker container."""
    # Run from the EC root, where twister's relative paths are rooted. Pass
    # the arguments positionally so that bash does not re-split them.
    cmd = [
        str(SCRIPT_DIR / "run_docker.sh"),
        "bash",
        "-c",
        'cd /workspace/src/platform/ec && exec ./dagwood "$@"',
        "dagwood",
        *sys.argv[1:],
    ]
    print(f"Running command: {' '.join(cmd)}")
    try:
        os.execvp(cmd[0], cmd)
    except OSError as e:
        print(f"Error executing {cmd[0]}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
