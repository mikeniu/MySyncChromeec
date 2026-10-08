# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions to apply patches and source overrides to the checkout."""

import contextlib
import logging
import os
import pathlib
import shutil
import subprocess


logger = logging.getLogger("zmake.repository_prep")


class RepositoryPrepError(RuntimeError):
    """Raised when codebase preparation fails."""


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
    current_path = pathlib.Path(path).resolve()
    for parent in [current_path] + list(current_path.parents):
        if (parent / ".git").exists():
            return parent
    return None


def get_dirty_files(repo_dir, checkout_root):
    """Get set of dirty/untracked files in a repo relative to checkout root."""
    repo_path = pathlib.Path(repo_dir)
    checkout_path = pathlib.Path(checkout_root).resolve()

    result = subprocess.run(
        ["git", "status", "--porcelain", "-uall", "-z"],
        cwd=repo_path,
        capture_output=True,
        check=True,
        text=True,
    )

    dirty_files = set()
    tokens = result.stdout.split("\0")
    idx = 0
    while idx < len(tokens):
        entry = tokens[idx]
        idx += 1
        if len(entry) < 4:
            continue
        status = entry[:2]
        rel_paths = [entry[3:]]
        if ("R" in status or "C" in status) and idx < len(tokens):
            if tokens[idx]:
                rel_paths.append(tokens[idx])
            idx += 1
        for file_rel in rel_paths:
            raw_path = repo_path / file_rel
            abs_path = raw_path.parent.resolve() / raw_path.name
            with contextlib.suppress(ValueError):
                dirty_files.add(abs_path.relative_to(checkout_path))
    return dirty_files


def find_target_repos(checkout_root, src_override_dir, patches_dir):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    root_path = pathlib.Path(checkout_root).resolve()

    if src_override_dir:
        override_path = pathlib.Path(src_override_dir).resolve()
        if override_path.exists():
            for src_file in _find_override_files(override_path):
                rel_path = src_file.relative_to(override_path)
                real_path = root_path / rel_path
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    if patches_dir:
        patches_path = pathlib.Path(patches_dir).resolve()
        if patches_path.exists():
            for patch_file in _find_patch_files(patches_path):
                rel_path = patch_file.relative_to(patches_path)
                repo_rel_path = rel_path.parent
                repo = (root_path / repo_rel_path).resolve()
                repo_git = find_git_repo(repo)
                if repo_git:
                    repos.add(repo_git)
    return repos


def _find_override_files(src_override_dir):
    """Yield all non-ignored override files recursively from src_override_dir."""
    for root, dirs, files in os.walk(src_override_dir):
        if ".git" in dirs:
            dirs.remove(".git")
        for file in files:
            if (
                file.startswith(".git")
                or file == ".keep"
                or file.endswith(".md")
                or file in ["OWNERS", "DIR_METADATA", "LICENSE"]
            ):
                continue
            yield pathlib.Path(root) / file


def _find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, dirs, files in os.walk(patches_dir):
        if ".git" in dirs:
            dirs.remove(".git")
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
        parts = line.split(maxsplit=2)
        if len(parts) >= 3:
            modified_files.append(pathlib.Path(parts[2].strip('"')))
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
        try:
            self.prepare()
        except BaseException:
            self.restore()
            raise
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.restore()

    def _check_file_dirty(self, abs_path, rel_path):
        if self.user_dirty_files and rel_path in self.user_dirty_files:
            msg = (
                f"File {abs_path} has local changes. "
                "Aborting to prevent overwriting."
            )
            logger.error("%s", msg)
            raise RepositoryPrepError(msg)

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
        self.revert_patches()
        self.revert_overrides()

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
        copied_files_raw = sorted(_find_override_files(self.src_override_dir))

        for src_file in copied_files_raw:
            rel_path = src_file.relative_to(self.src_override_dir)
            target_file = self.checkout_root / rel_path

            existed_before = target_file.exists() or target_file.is_symlink()
            if existed_before:
                self._check_file_dirty(target_file, rel_path)

                was_tracked = (
                    subprocess.run(
                        [
                            "git",
                            "ls-files",
                            "--error-unmatch",
                            "--",
                            target_file.name,
                        ],
                        cwd=target_file.parent,
                        capture_output=True,
                        check=False,
                    ).returncode
                    == 0
                )

                if not was_tracked:
                    msg = (
                        f"File {target_file} is untracked but exists. "
                        "Aborting to prevent overwriting."
                    )
                    logger.error("%s", msg)
                    raise RepositoryPrepError(msg)

            # Create parent directories if they don't exist
            target_file.parent.mkdir(parents=True, exist_ok=True)

            logger.info("Copying %s to %s", src_file, target_file)
            try:
                if target_file.is_symlink():
                    target_file.unlink()
                shutil.copy2(src_file, target_file)
                self.copied_files.append((target_file, existed_before))
            except (OSError, shutil.Error) as e:
                msg = f"Failed to copy {src_file} to {target_file}: {e}"
                logger.error("%s", msg)
                raise RepositoryPrepError(msg) from e

    def revert_overrides(self):
        """Revert applied source overrides."""
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                logger.info("Restoring %s from git", target_file)
                try:
                    subprocess.run(
                        ["git", "restore", "--", target_file.name],
                        cwd=target_file.parent,
                        check=True,
                    )
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to restore %s: %s", target_file, e)
            else:
                logger.info("Removing %s", target_file)
                try:
                    target_file.unlink()
                except FileNotFoundError:
                    pass
                parent = target_file.parent
                while parent != self.checkout_root and parent.is_relative_to(
                    self.checkout_root
                ):
                    try:
                        parent.rmdir()
                        logger.info("Removed empty directory %s", parent)
                        parent = parent.parent
                    except OSError:
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

            logger.info("Processing patch %s for %s", patch_file, target_dir)
            can_apply = subprocess.run(
                ["git", "apply", "--check", str(patch_file)],
                cwd=target_dir,
                capture_output=True,
                check=False,
            )
            if can_apply.returncode != 0:
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
                msg = (
                    f"Failed to get modified files for patch {patch_file}: {e}"
                )
                logger.error("%s", msg)
                raise RepositoryPrepError(msg) from e

            try:
                if can_apply.returncode == 0:
                    subprocess.run(
                        ["git", "apply", str(patch_file)],
                        cwd=target_dir,
                        capture_output=True,
                        check=True,
                    )
                    self.applied_patches.append((patch_file, target_dir))
                else:
                    stderr = can_apply.stderr.decode("utf-8", errors="replace")
                    msg = f"Patch {patch_file} cannot be applied:\n{stderr}"
                    logger.error("%s", msg)
                    raise RepositoryPrepError(msg)
            except subprocess.CalledProcessError as e:
                msg = f"Failed to apply patch {patch_file}: {e}"
                logger.error("%s", msg)
                raise RepositoryPrepError(msg) from e

    def revert_patches(self):
        """Revert applied patches."""
        for patch_file, target_dir in reversed(self.applied_patches):
            logger.info("Reverting patch %s from %s", patch_file, target_dir)
            try:
                subprocess.run(
                    ["git", "apply", "--reverse", str(patch_file)],
                    cwd=target_dir,
                    check=True,
                )
            except subprocess.CalledProcessError as e:
                logger.error("Failed to revert patch %s: %s", patch_file, e)
