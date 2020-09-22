#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Build and test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.
"""

import argparse
import multiprocessing
import os
import subprocess
import sys


def main(args):
    """Builds all of the EC targets"""
    opts = ParseArgs(args)

    result = subprocess.run(['make', 'buildall', '-j{}'.format(opts.cpus)],
                            cwd=os.path.dirname(sys.argv[0]))
    return result.returncode


def ParseArgs(args):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        '--cpus',
        dest='cpus',
        default=multiprocessing.cpu_count(),
        help='The number of cores to use.',
    )

    return parser.parse_args(args)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
