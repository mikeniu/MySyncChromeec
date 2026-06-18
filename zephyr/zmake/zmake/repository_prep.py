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


def get_default_patches_dirs(checkout_root):
    """Get the default list of patches directories."""
    checkout_root = pathlib.Path(checkout_root)
    return [
        checkout_root
        / "src"
        / "platform"
        / "ec"
        / "upstream_overrides"
        / "patches",
        checkout_root / "src" / "platform" / "ec-private" / "patches",
    ]


def get_default_src_override_dirs(checkout_root):
    """Get the default list of source override directories."""
    checkout_root = pathlib.Path(checkout_root)
    return [
        checkout_root
        / "src"
        / "platform"
        / "ec"
        / "upstream_overrides"
        / "src-override",
        checkout_root / "src" / "platform" / "ec-private" / "src-override",
    ]


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


def find_target_repos(checkout_root, src_override_dirs, patches_dirs):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    checkout_root = pathlib.Path(checkout_root).resolve()

    for override_dir in src_override_dirs:
        override_dir = pathlib.Path(override_dir)
        if override_dir.exists():
            for src_file in _find_override_files(override_dir):
                rel_path = src_file.relative_to(override_dir)
                real_path = checkout_root / rel_path
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    for patches_dir in patches_dirs:
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


class CodebasePreparer:
    """Manages the application and reversion of patches and overrides."""

    def __init__(
        self,
        checkout_root,
        patches_dirs,
        src_override_dirs,
        user_dirty_files=None,
        commit=False,
    ):
        self.checkout_root = pathlib.Path(checkout_root).resolve()
        self.patches_dirs = [pathlib.Path(d) for d in patches_dirs]
        self.src_override_dirs = [pathlib.Path(d) for d in src_override_dirs]
        self.user_dirty_files = user_dirty_files
        self.commit = commit
        self.applied_patches = []
        self.copied_files = []

    def __enter__(self):
        self.prepare()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.restore()

    def _check_file_dirty(self, target_file, rel_path):
        """Check if a file is dirty in user workspace."""
        if self.user_dirty_files is not None:
            if rel_path in self.user_dirty_files:
                logger.error(
                    "File %s has local changes or is untracked (from dirty list). "
                    "Aborting to prevent data loss.",
                    target_file,
                )
                sys.exit(1)
        else:
            # Check dirty locally using git diff
            repo = find_git_repo(target_file)
            if repo:
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
                    logger.error(
                        "File %s has local changes. Aborting to prevent data loss.",
                        target_file,
                    )
                    sys.exit(1)

    def prepare(self):
        """Apply overrides and then patches."""
        self.copy_source_overrides()
        self.apply_patches()
        if self.commit:
            self.commit_changes()

    def restore(self):
        """Revert overrides and patches in reverse order."""
        if self.commit:
            logger.debug("Skipping restore for committed shadow workspace.")
            return
        logger.info("Restoring codebase to clean state...")
        self.revert_source_overrides()
        self.revert_patches()

    def commit_changes(self):
        """Commit applied overrides and patches in shadow repositories."""
        target_repos = find_target_repos(
            self.checkout_root, self.src_override_dirs, self.patches_dirs
        )
        for repo in target_repos:
            # repo is absolute path in shadow checkout
            if repo.exists() and not repo.is_symlink():
                logger.info("Committing applied changes in %s", repo)
                try:
                    subprocess.run(
                        ["git", "add", "-A"],
                        cwd=repo,
                        capture_output=True,
                        check=True,
                    )
                    env = os.environ.copy()
                    env["GIT_AUTHOR_NAME"] = "Shadow Builder"
                    env["GIT_AUTHOR_EMAIL"] = "shadow@builder.org"
                    env["GIT_COMMITTER_NAME"] = "Shadow Builder"
                    env["GIT_COMMITTER_EMAIL"] = "shadow@builder.org"

                    status = subprocess.run(
                        ["git", "status", "--porcelain"],
                        cwd=repo,
                        capture_output=True,
                        check=True,
                        text=True,
                    )
                    if status.stdout.strip():
                        subprocess.run(
                            [
                                "git",
                                "commit",
                                "-m",
                                "applied patches and overrides",
                            ],
                            cwd=repo,
                            capture_output=True,
                            check=True,
                            env=env,
                        )
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to commit changes in %s: %s", repo, e)

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
            copied_files_raw = list(_find_override_files(src_override_dir))

            for src_file in copied_files_raw:
                rel_path = src_file.relative_to(src_override_dir)
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
                    sys.exit(1)

                logger.info(
                    "Processing patch %s for %s", patch_file, target_dir
                )
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
                        logger.info("Applied patch %s", patch_file)
                        self.applied_patches.append((patch_file, target_dir))
                    else:
                        # Check if already applied
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
                        else:
                            logger.error(
                                "Patch %s cannot be applied and "
                                "does not seem to be already applied.",
                                patch_file,
                            )
                            logger.error(
                                "stdout: %s", can_apply.stdout.decode()
                            )
                            logger.error(
                                "stderr: %s", can_apply.stderr.decode()
                            )
                            sys.exit(1)
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to apply patch %s: %s", patch_file, e)
                    sys.exit(1)

    def revert_source_overrides(self):
        """Restore or remove copied source overrides."""
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                logger.info("Restoring tracked file %s", target_file)
                try:
                    subprocess.run(
                        ["git", "restore", target_file.name],
                        cwd=target_file.parent,
                        check=True,
                    )
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to restore %s: %s", target_file, e)
            else:
                logger.info("Removing new file %s", target_file)
                try:
                    os.remove(target_file)
                    # Clean empty parent directories
                    parent = target_file.parent
                    while parent != self.checkout_root:
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


# Helper function to match the old API if needed, but updated for multiple paths
def prepare_codebase(
    checkout_root,
    patches_dirs,
    src_override_dirs,
    applied_patches,
    copied_files,
    user_dirty_files=None,
):
    """Apply patches and copy source overrides (legacy functional wrapper)."""
    preparer = CodebasePreparer(
        checkout_root, patches_dirs, src_override_dirs, user_dirty_files
    )
    preparer.prepare()
    # Mutate the passed lists to return the state (for legacy compatibility)
    applied_patches.extend(preparer.applied_patches)
    copied_files.extend(preparer.copied_files)
    return preparer


def _has_files(path):
    """Check if a path exists and contains files."""
    if not path:
        return False
    p = pathlib.Path(path)
    return p.exists() and any(p.iterdir())


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
        logger.info("Copying build outputs back to real checkout...")
        if real_build.exists():
            shutil.rmtree(real_build)
        shutil.copytree(shadow_build, real_build, symlinks=True)
        fix_symlinks(real_build, shadow_checkout, real_checkout)

    for shadow_path in shadow_ec.glob("twister-out*"):
        rel_path = shadow_path.relative_to(shadow_ec)
        real_path = real_ec / rel_path
        logger.info("Copying %s back to real checkout...", rel_path)
        if real_path.exists():
            shutil.rmtree(real_path)
        shutil.copytree(shadow_path, real_path, symlinks=True)
        fix_symlinks(real_path, shadow_checkout, real_checkout)


def _needs_copy_for_overrides(rel_path, src_override_dirs):
    """Check if the path has files in overrides."""
    for override_dir in src_override_dirs:
        override_dir = pathlib.Path(override_dir)
        if override_dir.exists():
            d = override_dir / rel_path
            if d.exists() and any(d.iterdir()):
                return True
    return False


def _needs_copy_for_patches(rel_path, patches_dirs):
    """Check if the path has patches."""
    for patches_dir in patches_dirs:
        patches_dir = pathlib.Path(patches_dir)
        if patches_dir.exists():
            d = patches_dir / rel_path
            if d.exists() and any(f.endswith(".patch") for f in os.listdir(d)):
                return True
    return False


def create_shadow_checkout(
    real_checkout,
    shadow_root,
    modules,
    zephyr_base,
    src_override_dirs,
    patches_dirs,
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
        else:
            need_copy = _needs_copy_for_overrides(
                rel_path, src_override_dirs
            ) or _needs_copy_for_patches(rel_path, patches_dirs)

        if need_copy:
            logger.info("Copying %s to shadow checkout...", rel_path)
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
            logger.debug("Symlinking %s to shadow checkout...", rel_path)
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            os.symlink(path, dst_path)
