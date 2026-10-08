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
    """Exception raised when repository preparation fails."""


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


def get_default_patches_dirs(checkout_root):
    """Get the default list of patches directories."""
    root_path = pathlib.Path(checkout_root)
    return [
        root_path
        / "src"
        / "platform"
        / "ec"
        / "upstream_overrides"
        / "patches",
        root_path / "src" / "platform" / "ec-private" / "patches",
    ]


def get_default_src_override_dirs(checkout_root):
    """Get the default list of source override directories."""
    root_path = pathlib.Path(checkout_root)
    return [
        root_path
        / "src"
        / "platform"
        / "ec"
        / "upstream_overrides"
        / "src-override",
        root_path / "src" / "platform" / "ec-private" / "src-override",
    ]


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


def find_target_repos(checkout_root, src_override_dirs, patches_dirs):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    root_path = pathlib.Path(checkout_root).resolve()

    for override_dir in src_override_dirs or []:
        override_path = pathlib.Path(override_dir).resolve()
        if override_path.exists():
            for src_file in _find_override_files(override_path):
                rel_path = src_file.relative_to(override_path)
                real_path = root_path / rel_path
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    for patches_dir in patches_dirs or []:
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
        if not line:
            continue
        parts = line.split(maxsplit=2)
        if len(parts) >= 3:
            files.append(pathlib.Path(parts[2].strip('"')))
    return files


class CodebasePreparer:
    """Manages the application and reversion of patches and overrides."""

    def __init__(
        self,
        checkout_root,
        patches_dirs,
        src_override_dirs,
        user_dirty_files=None,
    ):
        self.checkout_root = pathlib.Path(checkout_root).resolve()
        self.patches_dirs = [
            pathlib.Path(d).resolve() for d in (patches_dirs or [])
        ]
        self.src_override_dirs = [
            pathlib.Path(d).resolve() for d in (src_override_dirs or [])
        ]
        self.user_dirty_files = user_dirty_files
        self.applied_patches = []
        self.copied_files = []
        self.modified_by_prep = set()

    def __enter__(self):
        try:
            self.prepare()
        except BaseException:
            self.restore()
            raise
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.restore()

    def _check_file_dirty(self, target_file, rel_path):
        """Check if a file is dirty in user workspace."""
        if target_file.resolve() in self.modified_by_prep:
            return
        if self.user_dirty_files is not None:
            if rel_path in self.user_dirty_files:
                logger.error(
                    "File %s has local changes or is untracked (from dirty list). "
                    "Aborting to prevent data loss.",
                    target_file,
                )
                raise RepositoryPrepError(
                    f"File {target_file} has local changes or is untracked."
                )
        else:
            repo = find_git_repo(target_file.parent)
            if repo:
                lexical_target = target_file.parent.resolve() / target_file.name
                rel_in_repo = lexical_target.relative_to(repo.resolve())
                status_out = subprocess.run(
                    [
                        "git",
                        "status",
                        "--porcelain",
                        "--",
                        str(rel_in_repo),
                    ],
                    cwd=repo,
                    capture_output=True,
                    check=False,
                    text=True,
                ).stdout.strip()
                if status_out:
                    logger.error(
                        "File %s has local changes (%s). Aborting to prevent data loss.",
                        target_file,
                        status_out,
                    )
                    raise RepositoryPrepError(
                        f"File {target_file} has local changes: {status_out}"
                    )

    def prepare(self):
        """Apply overrides and then patches."""
        self.copy_source_overrides()
        self.apply_patches()

    def restore(self):
        """Revert patches and overrides in reverse order."""
        logger.info("Restoring codebase to clean state...")
        self.revert_patches()
        self.revert_source_overrides()

    def copy_source_overrides(self):
        """Copy source overrides to the checkout."""
        for src_override_dir in self.src_override_dirs:
            if not src_override_dir.exists():
                logger.debug(
                    "Source override directory %s does not exist. Skipping.",
                    src_override_dir,
                )
                continue

            logger.info("Copying files from %s", src_override_dir)
            copied_files_raw = sorted(_find_override_files(src_override_dir))

            for src_file in copied_files_raw:
                rel_path = src_file.relative_to(src_override_dir)
                target_file = self.checkout_root / rel_path

                already_modified = (
                    target_file.resolve() in self.modified_by_prep
                )
                existed_before = (
                    target_file.exists() or target_file.is_symlink()
                )
                if existed_before and not already_modified:
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
                        logger.error(
                            "File %s is untracked but exists. "
                            "Aborting to prevent overwriting.",
                            target_file,
                        )
                        raise RepositoryPrepError(
                            f"File {target_file} is untracked but exists."
                        )

                target_file.parent.mkdir(parents=True, exist_ok=True)

                logger.info("Copying %s to %s", src_file, target_file)
                try:
                    if target_file.is_symlink():
                        target_file.unlink()
                    shutil.copy2(src_file, target_file)
                    if not already_modified:
                        self.copied_files.append((target_file, existed_before))
                    self.modified_by_prep.add(target_file.resolve())
                except (OSError, shutil.Error) as e:
                    logger.error(
                        "Failed to copy %s to %s: %s", src_file, target_file, e
                    )
                    raise RepositoryPrepError(
                        f"Failed to copy {src_file} to {target_file}: {e}"
                    ) from e

    def apply_patches(self):
        """Apply patches recursively from patches_dirs to the checkout."""
        for patches_dir in self.patches_dirs:
            if not patches_dir.exists():
                logger.debug(
                    "Patches directory %s does not exist. Skipping.",
                    patches_dir,
                )
                continue

            logger.info("Applying patches from %s", patches_dir)
            patch_files = sorted(list(_find_patch_files(patches_dir)))

            for patch_file in patch_files:
                rel_path = patch_file.relative_to(patches_dir)
                target_rel_dir = rel_path.parent
                target_dir = self.checkout_root / target_rel_dir

                if not target_dir.exists():
                    logger.warning(
                        "Target directory %s for patch %s does not exist. Skipping.",
                        target_dir,
                        patch_file,
                    )
                    continue

                logger.info(
                    "Processing patch %s for %s", patch_file, target_dir
                )
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
                            "-R",
                            "--check",
                            str(patch_file),
                        ],
                        cwd=target_dir,
                        capture_output=True,
                        check=False,
                    )
                    if already_applied.returncode == 0:
                        logger.info(
                            "Patch %s already applied. Skipping.",
                            patch_file,
                        )
                        continue

                try:
                    modified_files = get_patch_modified_files(
                        patch_file, target_dir
                    )
                    for mod_file in modified_files:
                        abs_mod_file = (target_dir / mod_file).resolve()
                        rel_mod_file = abs_mod_file.relative_to(
                            self.checkout_root
                        )
                        self._check_file_dirty(abs_mod_file, rel_mod_file)
                except subprocess.CalledProcessError as e:
                    logger.error(
                        "Failed to get modified files for patch %s: %s",
                        patch_file,
                        e,
                    )
                    raise RepositoryPrepError(
                        f"Failed to get modified files for patch {patch_file}: {e}"
                    ) from e

                try:
                    if can_apply.returncode == 0:
                        subprocess.run(
                            ["git", "apply", str(patch_file)],
                            cwd=target_dir,
                            check=True,
                        )
                        logger.info("Applied patch %s", patch_file)
                        self.applied_patches.append((patch_file, target_dir))
                        for mod_file in modified_files:
                            self.modified_by_prep.add(
                                (target_dir / mod_file).resolve()
                            )
                    else:
                        logger.error(
                            "Patch %s cannot be applied and "
                            "does not seem to be already applied.",
                            patch_file,
                        )
                        logger.error("stdout: %s", can_apply.stdout.decode())
                        logger.error("stderr: %s", can_apply.stderr.decode())
                        raise RepositoryPrepError(
                            f"Patch {patch_file} cannot be applied."
                        )
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to apply patch %s: %s", patch_file, e)
                    raise RepositoryPrepError(
                        f"Failed to apply patch {patch_file}: {e}"
                    ) from e

    def revert_source_overrides(self):
        """Restore or remove copied source overrides."""
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                logger.info("Restoring tracked file %s", target_file)
                try:
                    subprocess.run(
                        ["git", "restore", "--", target_file.name],
                        cwd=target_file.parent,
                        check=True,
                    )
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to restore %s: %s", target_file, e)
            else:
                logger.info("Removing new file %s", target_file)
                try:
                    os.remove(target_file)
                    parent = target_file.parent
                    while (
                        parent != self.checkout_root
                        and parent.is_relative_to(self.checkout_root)
                    ):
                        if not os.listdir(parent):
                            os.rmdir(parent)
                            parent = parent.parent
                        else:
                            break
                except OSError as e:
                    logger.error("Failed to remove %s: %s", target_file, e)

    def revert_patches(self):
        """Revert applied patches in reverse order."""
        for patch_file, target_dir in reversed(self.applied_patches):
            logger.info("Reverting patch %s", patch_file)
            try:
                subprocess.run(
                    ["git", "apply", "-R", str(patch_file)],
                    cwd=target_dir,
                    check=True,
                )
            except subprocess.CalledProcessError as e:
                logger.error("Failed to revert patch %s: %s", patch_file, e)
