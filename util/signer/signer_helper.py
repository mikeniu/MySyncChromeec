#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper script for codesigner to handle H1 signing.

The path to codesigner executable and its command line parameters come from the
invoking Makefile.

In all branches but MP this script just invokes the codesigner, in the MP branch
the script generates two sets of hashes and manifests, for release candidate and
general releases.  The RC versions will have 0x10000 bit set in the board id
flags in the manifest.
"""

import argparse
import hashlib
import json
import os
import sys


ROOT_DIR = os.path.dirname(os.path.abspath(sys.argv[0]))
sys.path.append(
    os.path.join(ROOT_DIR, "..", "..", "..", "ti50", "common", "tools")
)

import common


def make_rc_filename(filename):
    """Add rc. prefix to the file name for RC release signings."""
    return os.path.join(
        os.path.dirname(filename), "rc." + os.path.basename(filename)
    )


def run_codesigner(base_cmd, output, manifest, hashes, rc):
    # When signig RO the manifest is not supplied.
    if manifest:
        with open(manifest, "r", encoding="utf-8") as m:
            m_data = json.load(m)
        bid_flags = m_data.get("board_id_flags")
        if bid_flags is not None:
            if rc:
                hashes = make_rc_filename(hashes)
                output = make_rc_filename(output)
                manifest = make_rc_filename(manifest)
                m_data["board_id_flags"] = bid_flags | 0x10000
            else:
                m_data["board_id_flags"] = bid_flags & ~0x10000

            with open(manifest, "w", encoding="utf-8") as m:
                json.dump(m_data, m, indent=2)

    full_cmd = list(base_cmd)
    if manifest:
        full_cmd += ["-j", manifest]
    if hashes:
        full_cmd += ["--hashes", hashes]
    full_cmd += ["--output", output]
    common.cmd_run(full_cmd)

    if not hashes:
        return

    # Let's generate the digest of the combined hashes
    with open(hashes + ".hashes", "rb") as f:
        digest_data = hashlib.sha256(f.read()).digest()
        digest_file = ".".join(hashes.split(".")[:-1]) + ".digest"
        with open(digest_file, "wb") as f:
            f.write(digest_data)


def main(args):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--branch", default="")
    parser.add_argument("--signer", required=True)
    parser.add_argument("-j", "--json", dest="manifest")
    parser.add_argument("--hashes")
    parser.add_argument("--output")

    # All other arguments are passed through to codesigner
    args, unknown_args = parser.parse_known_args(args)
    if not args.output:
        raise common.ToolsException("--output is required")

    # Base signer command
    base_cmd = [args.signer] + unknown_args

    run_codesigner(base_cmd, args.output, args.manifest, args.hashes, False)
    this_is_rc = args.branch == "MP" and args.manifest and args.hashes
    if this_is_rc:
        run_codesigner(base_cmd, args.output, args.manifest, args.hashes, True)


if __name__ == "__main__":
    common.starter(main, args=sys.argv[1:])
