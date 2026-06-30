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


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    for path in pathlib.Path(__file__).resolve().parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


def apply_patches(opts, applied_patches):
    """Apply patches recursively from patches_dir to the checkout."""
    patches_dir = pathlib.Path(opts.patches_dir)
    checkout_root = find_checkout()

    if patches_dir.exists():
        patch_files = []
        for root, _, files in os.walk(patches_dir):
            for file in files:
                if file.endswith(".patch"):
                    patch_files.append(pathlib.Path(root) / file)

        patch_files.sort()

        for patch_file in patch_files:
            rel_path = patch_file.relative_to(patches_dir)
            target_rel_dir = rel_path.parent
            target_dir = checkout_root / target_rel_dir

            if not target_dir.exists():
                print(
                    f"Warning: Target directory {target_dir} for patch "
                    f"{patch_file} does not exist. Skipping."
                )
                continue

            print(f"Processing patch {patch_file} for {target_dir}")
            try:
                # Check if it can be applied
                can_apply = subprocess.run(
                    ["git", "apply", "--check", str(patch_file)],
                    cwd=target_dir,
                    capture_output=True,
                    check=False,
                )
                if can_apply.returncode == 0:
                    subprocess.run(
                        ["git", "apply", str(patch_file)],
                        cwd=target_dir,
                        check=True,
                    )
                    print(f"Applied patch {patch_file}")
                    applied_patches.append((patch_file, target_dir))
                else:
                    # Check if already applied
                    already_applied = subprocess.run(
                        ["git", "apply", "-R", "--check", str(patch_file)],
                        cwd=target_dir,
                        capture_output=True,
                        check=False,
                    )
                    if already_applied.returncode == 0:
                        print(f"Patch {patch_file} already applied. Skipping.")
                    else:
                        print(
                            f"Error: Patch {patch_file} cannot be applied "
                            "and does not seem to be already applied."
                        )
                        print(f"stdout: {can_apply.stdout.decode()}")
                        print(f"stderr: {can_apply.stderr.decode()}")
                        sys.exit(1)
            except subprocess.CalledProcessError as e:
                print(f"Failed to apply patch {patch_file}: {e}")
                sys.exit(1)
    else:
        print(
            f"Patches directory {patches_dir} does not exist. "
            "Skipping patch application."
        )


def copy_source_overrides(opts, copied_files):
    """Copy source overrides to the checkout."""
    if not hasattr(opts, "src_override_dir"):
        return

    src_override_dir = pathlib.Path(opts.src_override_dir)
    if not src_override_dir.exists():
        print(
            f"Source override directory {src_override_dir} does not exist. "
            "Skipping file copying."
        )
        return

    print(f"Copying files from {src_override_dir}")
    checkout_root = find_checkout()

    copied_files_raw = []
    for root, _, files in os.walk(src_override_dir):
        for file in files:
            if (
                file.startswith(".git")
                or file.endswith(".md")
                or file in ["OWNERS", "DIR_METADATA", "LICENSE"]
            ):
                continue
            copied_files_raw.append(pathlib.Path(root) / file)

    if not copied_files_raw:
        print(f"No files found in {src_override_dir}.")
        return

    for src_file in copied_files_raw:
        rel_path = src_file.relative_to(src_override_dir)
        target_file = checkout_root / rel_path

        existed_before = target_file.exists()
        if existed_before:
            # Check if tracked
            was_tracked = (
                subprocess.run(
                    ["git", "ls-files", "--error-unmatch", target_file.name],
                    cwd=target_file.parent,
                    capture_output=True,
                    check=False,
                ).returncode
                == 0
            )

            if not was_tracked:
                print(
                    f"Error: File {target_file} is untracked but exists. "
                    "Aborting to prevent overwriting."
                )
                sys.exit(1)

            # Check if dirty
            is_dirty = (
                subprocess.run(
                    ["git", "diff", "--name-only", target_file.name],
                    cwd=target_file.parent,
                    capture_output=True,
                    check=False,
                ).stdout.strip()
                != b""
            )
            if is_dirty:
                print(
                    f"Error: File {target_file} has local changes. "
                    "Aborting to prevent data loss."
                )
                sys.exit(1)

        # Create parent directories if they don't exist
        target_file.parent.mkdir(parents=True, exist_ok=True)

        print(f"Copying {src_file} to {target_file}")
        try:
            shutil.copy2(src_file, target_file)
            copied_files.append((target_file, existed_before))
        except (OSError, shutil.Error) as e:
            print(f"Failed to copy {src_file} to {target_file}: {e}")
            sys.exit(1)


def revert_source_overrides(copied_files):
    """Restore or remove copied source overrides."""
    for target_file, existed_before in reversed(copied_files):
        if existed_before:
            print(f"Restoring tracked file {target_file}")
            try:
                subprocess.run(
                    ["git", "restore", target_file.name],
                    cwd=target_file.parent,
                    check=True,
                )
            except subprocess.CalledProcessError as e:
                print(f"Failed to restore {target_file}: {e}")
        else:
            print(f"Removing new file {target_file}")
            try:
                os.remove(target_file)
                # Clean empty parent directories
                parent = target_file.parent
                checkout_root = find_checkout()
                while parent != checkout_root:
                    if not os.listdir(parent):
                        os.rmdir(parent)
                        parent = parent.parent
                    else:
                        break
            except OSError as e:
                print(f"Failed to remove {target_file}: {e}")


def revert_patches(applied_patches):
    """Revert applied patches in reverse order."""
    for patch_file, target_dir in reversed(applied_patches):
        print(f"Reverting patch {patch_file}")
        try:
            subprocess.run(
                ["git", "apply", "-R", str(patch_file)],
                cwd=target_dir,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(f"Failed to revert patch {patch_file}: {e}")


def prepare_codebase(opts, applied_patches, copied_files):
    """Apply patches and copy source overrides to the checkout."""
    apply_patches(opts, applied_patches)
    copy_source_overrides(opts, copied_files)


def restore_codebase(applied_patches, copied_files):
    """Revert applied patches and restore/remove copied overrides."""
    print("Restoring codebase to clean state...")
    revert_source_overrides(copied_files)
    revert_patches(applied_patches)


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
        # TODO(b/180008931): make this required=True.
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
