# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions to apply patches and source overrides to the checkout."""

import logging
import os
import pathlib
import shutil
import subprocess
import sys


logger = logging.getLogger("zmake.repository_prep")

_CONFIG = {
    "checkout_path": None,
}


def set_checkout(path):
    """Override the checkout path to be used by find_checkout."""
    old_path = _CONFIG["checkout_path"]
    if path is None:
        _CONFIG["checkout_path"] = None
    else:
        _CONFIG["checkout_path"] = pathlib.Path(path)
    return old_path


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    if _CONFIG["checkout_path"]:
        return _CONFIG["checkout_path"]
    for path in pathlib.Path(__file__).resolve().parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


def get_default_patches_dir(checkout_root):
    """Get the default patches directory."""
    return (
        pathlib.Path(checkout_root)
        / "src"
        / "platform"
        / "ec-private"
        / "patches"
    )


def get_default_src_override_dir(checkout_root):
    """Get the default source override directory."""
    return (
        pathlib.Path(checkout_root)
        / "src"
        / "platform"
        / "ec-private"
        / "src-override"
    )


def find_git_repo(path):
    """Find the git repository root for a given path."""
    path = pathlib.Path(path).resolve()
    for parent in [path] + list(path.parents):
        if (parent / ".git").is_dir():
            return parent
    return None


def get_dirty_files(repo_dir, checkout_root):
    """Get list of dirty files in a repo relative to checkout root."""
    repo_dir = pathlib.Path(repo_dir).resolve()
    checkout_root = pathlib.Path(checkout_root).resolve()

    # Get status of files (modified, untracked, etc.)
    result = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repo_dir,
        capture_output=True,
        check=True,
        text=True,
    )

    dirty_files = []
    for line in result.stdout.splitlines():
        if not line:
            continue
        # Format is "XY path"
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            rel_path = parts[1]
            abs_path = (repo_dir / rel_path).resolve()
            try:
                dirty_files.append(abs_path.relative_to(checkout_root))
            except ValueError:
                # File is outside checkout root, shouldn't happen for target repos
                pass
    return dirty_files


def find_target_repos(checkout_root, src_override_dir, patches_dir):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    checkout_root = pathlib.Path(checkout_root).resolve()

    if src_override_dir:
        src_override_dir = pathlib.Path(src_override_dir)
        if src_override_dir.exists():
            for src_file in _find_override_files(src_override_dir):
                rel_path = src_file.relative_to(src_override_dir)
                real_path = checkout_root / rel_path
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    if patches_dir:
        patches_dir = pathlib.Path(patches_dir)
        if patches_dir.exists():
            for patch_file in _find_patch_files(patches_dir):
                rel_path = patch_file.relative_to(patches_dir)
                repo_rel_path = rel_path.parent
                repo = (checkout_root / repo_rel_path).resolve()
                repo_git = find_git_repo(repo)
                if repo_git:
                    repos.add(repo_git)
    return repos


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


def _find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, _, files in os.walk(patches_dir):
        for file in files:
            if file.endswith(".patch"):
                yield pathlib.Path(root) / file


def get_patch_modified_files(patch_file, target_dir):
    """Get list of files modified by a patch."""
    result = subprocess.run(
        ["git", "apply", "--numstat", str(patch_file)],
        cwd=target_dir,
        capture_output=True,
        check=True,
        text=True,
    )
    modified_files = []
    for line in result.stdout.splitlines():
        if not line:
            continue
        parts = line.split()
        if len(parts) >= 3:
            # Format: added_lines deleted_lines filename
            modified_files.append(parts[2])
    return modified_files


class CodebasePreparer:
    """Context manager to prepare codebase for build and restore it after."""

    def __init__(
        self,
        checkout_root,
        patches_dir,
        src_override_dir,
        user_dirty_files=None,
    ):
        self.checkout_root = pathlib.Path(checkout_root).resolve()
        self.patches_dir = (
            pathlib.Path(patches_dir).resolve() if patches_dir else None
        )
        self.src_override_dir = (
            pathlib.Path(src_override_dir).resolve()
            if src_override_dir
            else None
        )
        self.user_dirty_files = user_dirty_files
        self.applied_patches = []
        self.copied_files = []

    def __enter__(self):
        self.prepare()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.restore()

    def _check_file_dirty(self, abs_path, rel_path):
        if self.user_dirty_files and rel_path in self.user_dirty_files:
            logger.error(
                "File %s has local changes. Aborting to prevent overwriting.",
                abs_path,
            )
            sys.exit(1)

    def prepare(self):
        """Apply overrides and patches."""
        if self.user_dirty_files is None:
            self.user_dirty_files = set()
            target_repos = find_target_repos(
                self.checkout_root, self.src_override_dir, self.patches_dir
            )
            for repo in target_repos:
                self.user_dirty_files.update(
                    get_dirty_files(repo, self.checkout_root)
                )

        self.apply_overrides()
        self.apply_patches()

    def restore(self):
        """Restore codebase to original state."""
        logger.info("Restoring codebase to clean state...")
        self.revert_overrides()
        self.revert_patches()

    def apply_overrides(self):
        """Copy source overrides to the checkout."""
        if not self.src_override_dir:
            return

        if not self.src_override_dir.exists():
            logger.debug(
                "Source override directory %s does not exist. Skipping.",
                self.src_override_dir,
            )
            return

        logger.info("Copying source overrides from %s", self.src_override_dir)
        copied_files_raw = list(_find_override_files(self.src_override_dir))

        for src_file in copied_files_raw:
            rel_path = src_file.relative_to(self.src_override_dir)
            target_file = self.checkout_root / rel_path

            existed_before = target_file.exists()
            if existed_before:
                self._check_file_dirty(target_file, rel_path)

                was_tracked = (
                    subprocess.run(
                        [
                            "git",
                            "ls-files",
                            "--error-unmatch",
                            target_file.name,
                        ],
                        cwd=target_file.parent,
                        capture_output=True,
                        check=False,
                    ).returncode
                    == 0
                )

                if not was_tracked:
                    logger.error(
                        "File %s is untracked but exists. "
                        "Aborting to prevent overwriting.",
                        target_file,
                    )
                    sys.exit(1)

            # Create parent directories if they don't exist
            target_file.parent.mkdir(parents=True, exist_ok=True)

            logger.info("Copying %s to %s", src_file, target_file)
            try:
                shutil.copy2(src_file, target_file)
                self.copied_files.append((target_file, existed_before))
            except (OSError, shutil.Error) as e:
                logger.error(
                    "Failed to copy %s to %s: %s", src_file, target_file, e
                )
                sys.exit(1)

    def revert_overrides(self):
        """Revert applied source overrides."""
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                # Restore from git
                logger.info("Restoring %s from git", target_file)
                subprocess.run(
                    ["git", "checkout", "--quiet", target_file.name],
                    cwd=target_file.parent,
                    check=True,
                )
            else:
                # Remove file and empty parent directories
                logger.info("Removing %s", target_file)
                try:
                    target_file.unlink()
                except FileNotFoundError:
                    pass
                # Clean up empty parent directories up to checkout root
                parent = target_file.parent
                while parent != self.checkout_root and parent.is_relative_to(
                    self.checkout_root
                ):
                    try:
                        parent.rmdir()
                        logger.info("Removed empty directory %s", parent)
                        parent = parent.parent
                    except OSError:
                        # Directory is not empty
                        break

    def apply_patches(self):
        """Apply patches recursively from patches_dir to the checkout."""
        if not self.patches_dir:
            return

        if not self.patches_dir.exists():
            logger.debug(
                "Patches directory %s does not exist. Skipping.",
                self.patches_dir,
            )
            return

        logger.info("Applying patches from %s", self.patches_dir)
        patch_files = sorted(list(_find_patch_files(self.patches_dir)))

        for patch_file in patch_files:
            rel_path = patch_file.relative_to(self.patches_dir)
            target_rel_dir = rel_path.parent
            target_dir = self.checkout_root / target_rel_dir

            if not target_dir.exists():
                logger.warning(
                    "Target directory %s for patch %s does not exist. Skipping.",
                    target_dir,
                    patch_file,
                )
                continue

            try:
                modified_files = get_patch_modified_files(
                    patch_file, target_dir
                )
                for mod_file in modified_files:
                    abs_mod_file = (target_dir / mod_file).resolve()
                    rel_mod_file = abs_mod_file.relative_to(self.checkout_root)
                    self._check_file_dirty(abs_mod_file, rel_mod_file)
            except subprocess.CalledProcessError as e:
                logger.error(
                    "Failed to get modified files for patch %s: %s",
                    patch_file,
                    e,
                )
                sys.exit(1)

            logger.info("Processing patch %s for %s", patch_file, target_dir)
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
                        capture_output=True,
                        check=True,
                    )
                    self.applied_patches.append((patch_file, target_dir))
                else:
                    # Check if already applied
                    already_applied = subprocess.run(
                        [
                            "git",
                            "apply",
                            "--reverse",
                            "--check",
                            str(patch_file),
                        ],
                        cwd=target_dir,
                        capture_output=True,
                        check=False,
                    )
                    if already_applied.returncode == 0:
                        logger.info(
                            "Patch %s already applied. Skipping.", patch_file
                        )
                    else:
                        logger.error(
                            "Patch %s cannot be applied:\n%s",
                            patch_file,
                            can_apply.stderr.decode("utf-8"),
                        )
                        sys.exit(1)
            except subprocess.CalledProcessError as e:
                logger.error("Failed to apply patch %s: %s", patch_file, e)
                sys.exit(1)

    def revert_patches(self):
        """Revert applied patches."""
        for patch_file, target_dir in reversed(self.applied_patches):
            logger.info("Reverting patch %s from %s", patch_file, target_dir)
            subprocess.run(
                ["git", "apply", "--reverse", str(patch_file)],
                cwd=target_dir,
                check=True,
            )


def prepare_codebase(
    opts, applied_patches, copied_files, user_dirty_files=None
):
    """Legacy wrapper for prepare_codebase."""
    checkout = find_checkout()
    # We assume opts has src_override_dir and patches_dir (singular)
    src_override_dir = (
        opts.src_override_dir if hasattr(opts, "src_override_dir") else None
    )
    patches_dir = opts.patches_dir if hasattr(opts, "patches_dir") else None

    preparer = CodebasePreparer(
        checkout, patches_dir, src_override_dir, user_dirty_files
    )
    preparer.prepare()
    # Populate the legacy lists so restore_codebase works
    applied_patches.extend(preparer.applied_patches)
    copied_files.extend(preparer.copied_files)
