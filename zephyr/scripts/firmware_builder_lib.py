# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions shared across firmware builder scripts."""

import argparse
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys

from zmake.repository_prep import find_checkout


def fix_symlinks(directory, old_prefix, new_prefix):
    """Find and fix symlinks pointing to old_prefix to point to new_prefix."""
    for root, dirs, files in os.walk(directory):
        for d in dirs + files:
            path = pathlib.Path(root) / d
            if path.is_symlink():
                target = os.readlink(path)
                if target.startswith(str(old_prefix)):
                    new_target = target.replace(
                        str(old_prefix), str(new_prefix), 1
                    )
                    path.unlink()
                    os.symlink(new_target, path)


def copy_outputs_back(shadow_checkout, real_checkout, ec_rel_path):
    """Copy build and twister outputs back to real checkout and fix symlinks."""
    shadow_ec = shadow_checkout / ec_rel_path
    real_ec = real_checkout / ec_rel_path

    shadow_build = shadow_ec / "build"
    real_build = real_ec / "build"
    if shadow_build.exists():
        print("Copying build outputs back to real checkout...")
        if real_build.exists():
            shutil.rmtree(real_build)
        shutil.copytree(shadow_build, real_build, symlinks=True)
        fix_symlinks(real_build, shadow_checkout, real_checkout)

    for shadow_path in shadow_ec.glob("twister-out*"):
        rel_path = shadow_path.relative_to(shadow_ec)
        real_path = real_ec / rel_path
        print(f"Copying {rel_path} back to real checkout...")
        if real_path.exists():
            shutil.rmtree(real_path)
        shutil.copytree(shadow_path, real_path, symlinks=True)
        fix_symlinks(real_path, shadow_checkout, real_checkout)


def create_shadow_checkout(
    real_checkout, shadow_root, modules, zephyr_base, code_override_path
):
    """Create a shadow checkout with copies of target repos and symlinks for others."""
    real_checkout = pathlib.Path(real_checkout).resolve()
    shadow_root = pathlib.Path(shadow_root).resolve()

    paths_to_handle = {
        "zephyr_base": zephyr_base,
    }

    for name, path in modules.items():
        paths_to_handle[f"module:{name}"] = path

    for key, path in paths_to_handle.items():
        if not path.exists():
            continue
        rel_path = path.resolve().relative_to(real_checkout)
        dst_path = shadow_root / rel_path

        need_copy = False
        if key == "module:ec":
            need_copy = True
        elif code_override_path and code_override_path.exists():
            # Check overrides
            override_path = code_override_path / "src-override" / rel_path
            if override_path.exists():
                need_copy = True
            # Check patches
            patch_path = code_override_path / "patches" / rel_path
            if patch_path.exists() and any(
                f.endswith(".patch") for f in os.listdir(patch_path)
            ):
                need_copy = True

        if need_copy:
            print(f"Copying {rel_path} to shadow checkout...")
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            shutil_ignore = shutil.ignore_patterns(
                ".git", "build", "twister-out", "twister-out.*"
            )
            shutil.copytree(path, dst_path, ignore=shutil_ignore, symlinks=True)
            # Init git
            subprocess.run(
                ["git", "init"], cwd=dst_path, capture_output=True, check=True
            )
            subprocess.run(
                ["git", "add", "-A"],
                cwd=dst_path,
                capture_output=True,
                check=True,
            )
            env = os.environ.copy()
            env["GIT_AUTHOR_NAME"] = "Shadow Builder"
            env["GIT_AUTHOR_EMAIL"] = "shadow@builder.org"
            env["GIT_COMMITTER_NAME"] = "Shadow Builder"
            env["GIT_COMMITTER_EMAIL"] = "shadow@builder.org"
            subprocess.run(
                ["git", "commit", "-m", "initial"],
                cwd=dst_path,
                capture_output=True,
                check=True,
                env=env,
            )
        else:
            print(f"Symlinking {rel_path} to shadow checkout...")
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            os.symlink(path, dst_path)


def create_arg_parser(build, bundle, test):
    """Parse all command line args and return opts dict."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--cpus",
        default=multiprocessing.cpu_count(),
        help="The number of cores to use.",
    )

    parser.add_argument(
        "--metrics",
        dest="metrics",
        required=True,
        help="File to write the json-encoded MetricsList proto message.",
    )

    parser.add_argument(
        "--metadata",
        required=False,
        help="Full pathname for the file in which to write build artifact metadata.",
    )

    parser.add_argument(
        "--output-dir",
        required=False,
        help="Full pathanme for the directory in which to bundle build artifacts.",
    )

    parser.add_argument(
        "--code-coverage",
        required=False,
        action="store_true",
        help="Build host-based unit tests for code coverage.",
    )

    parser.add_argument(
        "--bcs-version",
        dest="bcs_version",
        default="",
        required=False,
        help="BCS version to include in metadata.",
    )

    parser.add_argument(
        "--patches-dir",
        default=str(
            find_checkout() / "src" / "platform" / "ec-private" / "patches"
        ),
        help="Path to the patches directory",
    )

    parser.add_argument(
        "--src-override-dir",
        default=str(
            find_checkout() / "src" / "platform" / "ec-private" / "src-override"
        ),
        help="Path to the directory for source file overrides",
    )

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser("build", help="Builds all firmware targets")
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser(
        "bundle",
        help="Creates a tarball containing build artifacts from all firmware targets",
    )
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser("test", help="Runs all firmware unit tests")
    test_cmd.set_defaults(func=test)

    return parser, sub_cmds
