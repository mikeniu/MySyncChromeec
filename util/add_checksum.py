#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility to add RO SHA256 checksum."""

import argparse
from hashlib import sha256
import os.path
import subprocess
import sys


def get_fmap_area_prop(file, area_name):
    proc = subprocess.run(
        ["futility", "dump_fmap", "-p", file, area_name],
        check=True,
        capture_output=True,
    )
    # Output is like "EC_RO 4096 520192"
    area_prop = proc.stdout.split()
    assert len(area_prop) == 3, "Incorrect futility output"
    offset = int(area_prop[1])
    size = int(area_prop[2])

    return [offset, size]


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--image", help="Image to be modified", required=True)
    args = parser.parse_args()

    ec_checksum = os.path.join(os.path.dirname(args.image), "ec_hash.bin")

    ec_ro = get_fmap_area_prop(args.image, "EC_RO")
    ro_checksum = get_fmap_area_prop(args.image, "RO_CHECKSUM")
    # Make sure checksum is 32-bytes SHA256
    assert ro_checksum[1] == 32, "Invalid checksum size"
    # Make sure checksum is placed at the end of RO
    assert ro_checksum[0] == (
        ec_ro[0] + ec_ro[1] - 32
    ), "Invalid checksum offset"
    offset = ec_ro[0]
    size = ec_ro[1] - ro_checksum[1]

    with open(args.image, "rb") as f:
        image = bytearray(f.read())
        checksum = sha256(image[offset : offset + size])
        print("Generated checksum: " + checksum.hexdigest())

    image[offset + size : offset + size + 32] = checksum.digest()

    with open(ec_checksum, "wb") as f:
        f.write(image)

    print("Generated a new file: " + ec_checksum)


if __name__ == "__main__":
    sys.exit(main())
