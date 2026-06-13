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


_CONFIG = {
    "checkout_path": None,
}


def set_checkout(path):
    """Override the checkout path to be used by find_checkout."""
    if path is None:
        _CONFIG["checkout_path"] = None
    else:
        _CONFIG["checkout_path"] = pathlib.Path(path)


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    if _CONFIG["checkout_path"]:
        return _CONFIG["checkout_path"]
    for path in pathlib.Path(__file__).resolve().parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


def find_git_repo(path):
    """Find the git repository root for a given path."""
    path = pathlib.Path(path).absolute()
    if (path / ".git").exists():
        return path
    for parent in path.parents:
        if (parent / ".git").exists():
            return parent
    return None


def get_dirty_files(repo_dir, checkout_root):
    """Get list of dirty/untracked files in a repo, relative to checkout."""
    checkout_root = pathlib.Path(checkout_root).resolve()
    result = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repo_dir,
        capture_output=True,
        check=True,
        text=True,
    )
    dirty_files = set()
    for line in result.stdout.splitlines():
        if len(line) > 3:
            path_str = line[3:]
            if " -> " in path_str:
                _, path_str = path_str.split(" -> ", 1)
            abs_path = (repo_dir / path_str).resolve()
            try:
                rel_path = abs_path.relative_to(checkout_root)
                dirty_files.add(rel_path)
            except ValueError:
                pass
    return dirty_files


def _find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, _, files in os.walk(patches_dir):
        for file in files:
            if file.endswith(".patch"):
                yield pathlib.Path(root) / file


def _find_override_files(src_override_dir):
    """Yield all non-ignored override files recursively from src_override_dir."""
    for root, _, files in os.walk(src_override_dir):
        for file in files:
            if (
                file.startswith(".git")
                or file.endswith(".md")
                or file in ["OWNERS", "DIR_METADATA", "LICENSE"]
            ):
                continue
            yield pathlib.Path(root) / file


def find_target_repos(checkout_root, opts):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    checkout_root = pathlib.Path(checkout_root).resolve()

    if hasattr(opts, "src_override_dir"):
        src_override_dir = pathlib.Path(opts.src_override_dir)
        if src_override_dir.exists():
            for src_file in _find_override_files(src_override_dir):
                rel_path = src_file.relative_to(src_override_dir)
                real_path = checkout_root / rel_path
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    if hasattr(opts, "patches_dir"):
        patches_dir = pathlib.Path(opts.patches_dir)
        if patches_dir.exists():
            for patch_file in _find_patch_files(patches_dir):
                rel_path = patch_file.relative_to(patches_dir)
                repo_rel_path = rel_path.parent
                repo = (checkout_root / repo_rel_path).resolve()
                repo_git = find_git_repo(repo)
                if repo_git:
                    repos.add(repo_git)
    return repos


def get_patch_modified_files(patch_file, target_dir):
    """Get list of files modified by a patch file."""
    result = subprocess.run(
        ["git", "apply", "--numstat", str(patch_file)],
        cwd=target_dir,
        capture_output=True,
        check=True,
        text=True,
    )
    files = []
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            files.append(pathlib.Path(parts[2]))
    return files


def apply_patches(opts, applied_patches, user_dirty_files=None):
    """Apply patches recursively from patches_dir to the checkout."""
    if user_dirty_files is None:
        user_dirty_files = set()
    patches_dir = pathlib.Path(opts.patches_dir)
    checkout_root = find_checkout()

    if patches_dir.exists():
        patch_files = sorted(list(_find_patch_files(patches_dir)))

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

            try:
                modified_files = get_patch_modified_files(
                    patch_file, target_dir
                )
                for mod_file in modified_files:
                    abs_mod_file = (target_dir / mod_file).resolve()
                    rel_mod_file = abs_mod_file.relative_to(checkout_root)
                    if rel_mod_file in user_dirty_files:
                        print(
                            f"Error: Patch {patch_file} modifies file {abs_mod_file} "
                            "which has local changes. Aborting."
                        )
                        sys.exit(1)
            except subprocess.CalledProcessError as e:
                print(
                    f"Failed to get modified files for patch {patch_file}: {e}"
                )
                sys.exit(1)

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


def copy_source_overrides(opts, copied_files, user_dirty_files=None):
    """Copy source overrides to the checkout."""
    if user_dirty_files is None:
        user_dirty_files = set()
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

    copied_files_raw = list(_find_override_files(src_override_dir))

    if not copied_files_raw:
        print(f"No files found in {src_override_dir}.")
        return

    for src_file in copied_files_raw:
        rel_path = src_file.relative_to(src_override_dir)
        target_file = checkout_root / rel_path

        existed_before = target_file.exists()
        if existed_before:
            if rel_path in user_dirty_files:
                print(
                    f"Error: File {target_file} has local changes or is untracked. "
                    "Aborting to prevent data loss."
                )
                sys.exit(1)

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


def copy_outputs_back(shadow_checkout, real_checkout):
    """Copy build and twister outputs back to real checkout and fix symlinks."""
    shadow_ec = shadow_checkout / "src" / "platform" / "ec"
    real_ec = real_checkout / "src" / "platform" / "ec"

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


def prepare_codebase(
    opts, applied_patches, copied_files, user_dirty_files=None
):
    """Copy source overrides and then apply patches to the checkout."""
    if user_dirty_files is None:
        user_dirty_files = set()
    copy_source_overrides(opts, copied_files, user_dirty_files)
    apply_patches(opts, applied_patches, user_dirty_files)


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
