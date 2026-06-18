# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions to apply patches and source overrides to the checkout."""

import argparse
import contextlib
import logging
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

import zmake.modules


logger = logging.getLogger("zmake.repository_prep")

_CONFIG = {
    "checkout_path": None,
}
_SHADOW_GIT_ENV = {
    "GIT_AUTHOR_NAME": "Shadow Builder",
    "GIT_AUTHOR_EMAIL": "shadow@builder.org",
    "GIT_COMMITTER_NAME": "Shadow Builder",
    "GIT_COMMITTER_EMAIL": "shadow@builder.org",
}


def set_checkout(path):
    """Override the checkout path to be used by find_checkout."""
    old_path = _CONFIG["checkout_path"]
    _CONFIG["checkout_path"] = pathlib.Path(path) if path is not None else None
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
    root = pathlib.Path(checkout_root) / "src" / "platform"
    return [
        root / "ec" / "upstream_overrides" / "patches",
        root / "ec-private" / "patches",
    ]


def get_default_src_override_dirs(checkout_root):
    """Get the default list of source override directories."""
    root = pathlib.Path(checkout_root) / "src" / "platform"
    return [
        root / "ec" / "upstream_overrides" / "src-override",
        root / "ec-private" / "src-override",
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
            abs_path = (repo_dir / path_str.strip('"')).resolve()
            try:
                rel_path = abs_path.relative_to(checkout_root)
                dirty_files.add(rel_path)
            except ValueError:
                pass
    return dirty_files


def _resolve_target_path(checkout_root, rel_path, path_mappings=None):
    """Resolve a checkout-relative path against checkout_root."""
    checkout_root = pathlib.Path(checkout_root).resolve()
    rel_path = pathlib.Path(rel_path)
    if path_mappings:
        for prefix in sorted(
            path_mappings, key=lambda p: len(p.parts), reverse=True
        ):
            if rel_path == prefix or prefix in rel_path.parents:
                mapped_base = path_mappings[prefix]
                sub = rel_path.relative_to(prefix)
                return mapped_base if str(sub) == "." else mapped_base / sub

    direct = checkout_root / rel_path
    if direct.exists() or (checkout_root / "src").exists():
        return direct

    # Support ebuild WORKDIR layouts without top-level 'src/' directory
    if rel_path.parts and rel_path.parts[0] == "src":
        without_src = (
            pathlib.Path(*rel_path.parts[1:])
            if len(rel_path.parts) > 1
            else pathlib.Path(".")
        )
        candidate = checkout_root / without_src
        if candidate.exists() or (
            without_src.parts
            and (checkout_root / without_src.parts[0]).exists()
        ):
            return candidate

    # Support chromeos-zephyr ebuild layout (${S}/zephyrproject/...)
    zephyr_dir = checkout_root / "zephyrproject"
    if zephyr_dir.is_dir():
        mod_dir = zephyr_dir / "modules"
        zephyr_mappings = {
            pathlib.Path("src/platform/ec"): mod_dir / "ec",
            pathlib.Path("src/third_party/zephyrproject"): zephyr_dir,
            pathlib.Path("src/third_party/pigweed"): mod_dir / "pigweed",
            pathlib.Path("src/third_party/zephyr"): mod_dir,
        }
        for prefix, mapped_base in zephyr_mappings.items():
            if rel_path == prefix or prefix in rel_path.parents:
                sub = rel_path.relative_to(prefix)
                return mapped_base if str(sub) == "." else mapped_base / sub

    # Support running directly inside the EC repository root
    expected_repo = "ec" if (checkout_root / "zephyr").is_dir() else "ec-legacy"
    if (
        len(rel_path.parts) >= 3
        and rel_path.parts[:3] == ("src", "platform", expected_repo)
        and (checkout_root / "Makefile").exists()
    ):
        sub = (
            pathlib.Path(*rel_path.parts[3:])
            if len(rel_path.parts) > 3
            else pathlib.Path(".")
        )
        return checkout_root if str(sub) == "." else checkout_root / sub

    return direct


def find_target_repos(
    checkout_root, src_override_dirs, patches_dirs, path_mappings=None
):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    checkout_root = pathlib.Path(checkout_root).resolve()

    for override_dir in src_override_dirs:
        override_dir = pathlib.Path(override_dir)
        if override_dir.exists():
            for src_file in _find_override_files(override_dir):
                rel_path = src_file.relative_to(override_dir)
                real_path = _resolve_target_path(
                    checkout_root, rel_path, path_mappings
                )
                repo = find_git_repo(real_path)
                if repo:
                    repos.add(repo)

    for patches_dir in patches_dirs:
        patches_dir = pathlib.Path(patches_dir)
        if patches_dir.exists():
            for patch_file in _find_patch_files(patches_dir):
                rel_path = patch_file.relative_to(patches_dir)
                repo = _resolve_target_path(
                    checkout_root, rel_path.parent, path_mappings
                ).resolve()
                repo_git = find_git_repo(repo)
                if repo_git:
                    repos.add(repo_git)
    return repos


def _find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, dirs, files in os.walk(patches_dir):
        if ".git" in dirs:
            dirs.remove(".git")
        for file in files:
            if file.endswith(".patch"):
                yield pathlib.Path(root) / file


def _find_override_files(src_override_dir):
    """Yield non-ignored override files recursively from src_override_dir."""
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
        parts = line.split(maxsplit=2)
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
        path_mappings=None,
        allow_dirty=False,
    ):
        self.checkout_root = pathlib.Path(checkout_root).resolve()
        self.patches_dirs = [pathlib.Path(d) for d in patches_dirs]
        self.src_override_dirs = [pathlib.Path(d) for d in src_override_dirs]
        self.applied_patches = []
        self.copied_files = []
        self._state = {
            "user_dirty_files": user_dirty_files,
            "commit": commit,
            "allow_dirty": allow_dirty,
            "path_mappings": {
                pathlib.Path(k): pathlib.Path(v).resolve()
                for k, v in (path_mappings or {}).items()
            },
            "modified_by_prep": set(),
        }

    def __enter__(self):
        self.prepare()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.restore()

    def _resolve_target_path(self, rel_path):
        """Resolve a checkout-relative path against checkout_root."""
        return _resolve_target_path(
            self.checkout_root, rel_path, self._state["path_mappings"]
        )

    def _check_file_dirty(self, target_file, rel_path):
        """Check if a file is dirty in user workspace."""
        if self._state["allow_dirty"]:
            return
        if target_file.resolve() in self._state["modified_by_prep"]:
            return
        user_dirty_files = self._state["user_dirty_files"]
        if user_dirty_files is not None:
            rel_candidates = {rel_path}
            with contextlib.suppress(ValueError):
                rel_candidates.add(
                    target_file.resolve().relative_to(self.checkout_root)
                )
            if rel_candidates & user_dirty_files:
                logger.error(
                    "File %s has local changes or is untracked "
                    "(from dirty list). Aborting to prevent data loss.",
                    target_file,
                )
                sys.exit(1)
        else:
            repo = find_git_repo(target_file)
            if repo:
                rel_in_repo = target_file.resolve().relative_to(repo.resolve())
                is_dirty = (
                    subprocess.run(
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
                    ).stdout.strip()
                    != b""
                )
                if is_dirty:
                    logger.error(
                        "File %s has local changes. "
                        "Aborting to prevent data loss.",
                        target_file,
                    )
                    sys.exit(1)

    def prepare(self):
        """Apply overrides and then patches."""
        self.copy_source_overrides()
        self.apply_patches()
        if self._state["commit"]:
            self.commit_changes()

    def restore(self):
        """Revert patches and overrides in reverse order."""
        if self._state["commit"]:
            logger.debug("Skipping restore for committed shadow workspace.")
            return
        logger.info("Restoring codebase to clean state...")
        self.revert_patches()
        self.revert_source_overrides()

    def commit_changes(self):
        """Commit applied overrides and patches in shadow repositories."""
        target_repos = find_target_repos(
            self.checkout_root,
            self.src_override_dirs,
            self.patches_dirs,
            self._state["path_mappings"],
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
                    env = {**os.environ, **_SHADOW_GIT_ENV}

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
                target_file = self._resolve_target_path(rel_path)

                existed_before = target_file.exists()
                if existed_before:
                    self._check_file_dirty(target_file, rel_path)

                    if (
                        find_git_repo(target_file)
                        and target_file.resolve()
                        not in self._state["modified_by_prep"]
                        and not self._state["allow_dirty"]
                    ):
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
                    self._state["modified_by_prep"].add(target_file.resolve())
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
                target_dir = self._resolve_target_path(target_rel_dir)

                if not target_dir.exists():
                    logger.warning(
                        "Target directory %s for patch %s does not exist. Skipping.",
                        target_dir,
                        patch_file,
                    )
                    continue

                abs_mod_files = []
                try:
                    modified_files = get_patch_modified_files(
                        patch_file, target_dir
                    )
                    for mod_file in modified_files:
                        abs_mod_file = (target_dir / mod_file).resolve()
                        abs_mod_files.append(abs_mod_file)
                        self._check_file_dirty(
                            abs_mod_file, target_rel_dir / mod_file
                        )
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
                        self._state["modified_by_prep"].update(abs_mod_files)
                    else:
                        already_applied = subprocess.run(
                            ["git", "apply", "-R", "--check", str(patch_file)],
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
                                "Patch %s cannot be applied.\nstdout: %s\nstderr: %s",
                                patch_file,
                                can_apply.stdout.decode(),
                                can_apply.stderr.decode(),
                            )
                            sys.exit(1)
                except subprocess.CalledProcessError as e:
                    logger.error("Failed to apply patch %s: %s", patch_file, e)
                    sys.exit(1)

    def revert_source_overrides(self):
        """Restore or remove copied source overrides."""
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                if find_git_repo(target_file):
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
    """Check if path exists and contains non-ignored override files."""
    if not path:
        return False
    p = pathlib.Path(path)
    return p.exists() and any(_find_override_files(p))


def _has_patch_files(path):
    """Check if path exists and contains patch files."""
    if not path:
        return False
    p = pathlib.Path(path)
    return p.exists() and any(_find_patch_files(p))


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


def _remove_path(path):
    """Remove a file, symlink, or directory if it exists."""
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def _copytree_overwrite(src, dst):
    """Copy directory tree src to dst, overwriting existing files and symlinks."""
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        target = dst / item.name
        if item.is_symlink():
            _remove_path(target)
            os.symlink(os.readlink(item), target)
        elif item.is_dir():
            if target.is_symlink() or target.is_file():
                target.unlink()
            _copytree_overwrite(item, target)
        else:
            _remove_path(target)
            shutil.copy2(item, target)


def fix_lcov_info_paths(directory, old_prefix, new_prefix):
    """Replace old_prefix with new_prefix in *.info files under directory."""
    old_str, new_str = str(old_prefix), str(new_prefix)
    if not directory.exists() or old_str == new_str:
        return
    for info_file in directory.rglob("*.info"):
        if info_file.is_symlink() or not info_file.is_file():
            continue
        try:
            stat = info_file.stat()
            content = info_file.read_text(encoding="utf-8", errors="ignore")
            if old_str in content:
                info_file.write_text(
                    content.replace(old_str, new_str), encoding="utf-8"
                )
                os.utime(info_file, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        except OSError as e:
            logger.warning("Failed to update paths in %s: %s", info_file, e)


def _copy_build_metadata_to_shadow(real_checkout, shadow_checkout, ec_rel_path):
    """Copy .shard_info.json and *.info from real build dir to shadow build dir."""
    real_build = real_checkout / ec_rel_path / "build"
    shadow_build = shadow_checkout / ec_rel_path / "build"
    if not real_build.exists():
        return
    shard_file = real_build / "zephyr" / ".shard_info.json"
    if shard_file.is_file():
        dst_shard = shadow_build / "zephyr" / ".shard_info.json"
        dst_shard.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(shard_file, dst_shard)
    for info_file in real_build.rglob("*.info"):
        if info_file.is_file() and not info_file.is_symlink():
            dst_info = shadow_build / info_file.relative_to(real_build)
            dst_info.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(info_file, dst_info)
    fix_lcov_info_paths(shadow_build, real_checkout, shadow_checkout)


def copy_outputs_back(shadow_checkout, real_checkout, ec_rel_path):
    """Copy build and twister outputs back to real checkout and fix symlinks."""
    shadow_ec = shadow_checkout / ec_rel_path
    real_ec = real_checkout / ec_rel_path

    shadow_build = shadow_ec / "build"
    real_build = real_ec / "build"
    if shadow_build.exists() and not shadow_build.is_symlink():
        logger.info("Copying build outputs back to real checkout...")
        _copytree_overwrite(shadow_build, real_build)
        fix_symlinks(real_build, shadow_checkout, real_checkout)
        fix_lcov_info_paths(real_build, shadow_checkout, real_checkout)

    for shadow_path in shadow_ec.glob("twister-out*"):
        if shadow_path.is_symlink():
            continue
        rel_path = shadow_path.relative_to(shadow_ec)
        real_path = real_ec / rel_path
        logger.info("Copying %s back to real checkout...", rel_path)
        _copytree_overwrite(shadow_path, real_path)
        fix_symlinks(real_path, shadow_checkout, real_checkout)
        fix_lcov_info_paths(real_path, shadow_checkout, real_checkout)


def _needs_copy_for_overrides(rel_path, src_override_dirs):
    """Check if the path has files in overrides."""
    return any(
        _has_files(pathlib.Path(d) / rel_path) for d in src_override_dirs
    )


def _needs_copy_for_patches(rel_path, patches_dirs):
    """Check if the path has patches."""
    return any(
        _has_patch_files(pathlib.Path(d) / rel_path) for d in patches_dirs
    )


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
    (shadow_root / ".repo").mkdir(parents=True, exist_ok=True)

    paths_to_handle = {
        "zephyr_base": zephyr_base,
        "ec_private": real_checkout / "src" / "platform" / "ec-private",
        "chromiumos_overlay": (
            real_checkout / "src" / "third_party" / "chromiumos-overlay"
        ),
    }
    for name, path in modules.items():
        paths_to_handle[f"module:{name}"] = path
    for name in ("boringssl", "cryptoc", "eigen3", "googletest", "tpm2"):
        paths_to_handle[name] = real_checkout / "src" / "third_party" / name
    for name in ("project", "overlays", "private-overlays"):
        paths_to_handle[name] = real_checkout / "src" / name
    fp_dir = real_checkout / "src" / "platform" / "fingerprint"
    if fp_dir.is_dir():
        for sub in sorted(fp_dir.iterdir()):
            if sub.is_dir():
                paths_to_handle[f"fp:{sub.name}"] = sub

    seen_rel_paths = set()
    for key, path in paths_to_handle.items():
        if not path.exists():
            continue
        rel_path = path.resolve().relative_to(real_checkout)
        if rel_path in seen_rel_paths:
            continue
        seen_rel_paths.add(rel_path)
        dst_path = shadow_root / rel_path

        need_copy = (
            key == "module:ec"
            or _needs_copy_for_overrides(rel_path, src_override_dirs)
            or _needs_copy_for_patches(rel_path, patches_dirs)
        )

        dst_path.parent.mkdir(parents=True, exist_ok=True)
        if need_copy:
            logger.info("Copying %s to shadow checkout...", rel_path)
            shutil_ignore = shutil.ignore_patterns(
                ".git", "build", "twister-out*"
            )
            shutil.copytree(path, dst_path, ignore=shutil_ignore, symlinks=True)
            for cmd in (
                ["git", "init"],
                ["git", "config", "core.abbrev", "10"],
                ["git", "add", "-A"],
            ):
                subprocess.run(
                    cmd, cwd=dst_path, capture_output=True, check=True
                )
            subprocess.run(
                ["git", "commit", "-m", "initial"],
                cwd=dst_path,
                capture_output=True,
                check=True,
                env={**os.environ, **_SHADOW_GIT_ENV},
            )
        else:
            logger.debug("Symlinking %s to shadow checkout...", rel_path)
            os.symlink(path, dst_path)

    real_zp_git = (
        real_checkout / "src" / "third_party" / "zephyrproject" / ".git"
    )
    shadow_zp = shadow_root / "src" / "third_party" / "zephyrproject"
    if (
        real_zp_git.exists()
        and shadow_zp.is_dir()
        and not (shadow_zp / ".git").exists()
    ):
        os.symlink(real_zp_git.resolve(), shadow_zp / ".git")


@contextlib.contextmanager
def prepared_workspace(opts, modules=None):
    """Context manager to prepare a shadow or in-place workspace."""
    real_checkout = find_checkout()
    patches_dirs = getattr(opts, "patches_dirs", None) or (
        get_default_patches_dirs(real_checkout)
    )
    src_override_dirs = getattr(opts, "src_override_dirs", None) or (
        get_default_src_override_dirs(real_checkout)
    )

    has_overrides = any(_has_files(d) for d in src_override_dirs)
    has_patches = any(_has_patch_files(d) for d in patches_dirs)
    func_name = getattr(getattr(opts, "func", None), "__name__", "")
    use_shadow = (
        (has_overrides or has_patches)
        and not getattr(opts, "no_shadow", False)
        and func_name not in ("bundle", "merge_shards")
    )

    logging.basicConfig(level=logging.INFO)
    saved_env = {
        k: os.environ.get(k)
        for k in ("ZMAKE_WORKSPACE_PREPARED", "CROS_WORKON_SRCROOT")
    }

    def _restore_env():
        for k, v in saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    if use_shadow:
        print("Using shadow workspace for build...")
        target_repos = find_target_repos(
            real_checkout, src_override_dirs, patches_dirs
        )
        user_dirty_files = set()
        for repo in target_repos:
            user_dirty_files.update(get_dirty_files(repo, real_checkout))

        if modules is None:
            modules = zmake.modules.locate_from_checkout(real_checkout)
        zephyr_base = (
            real_checkout / "src" / "third_party" / "zephyrproject" / "zephyr"
        )
        ec_rel_path = modules["ec"].resolve().relative_to(real_checkout)

        with tempfile.TemporaryDirectory() as tmp_dir:
            shadow_checkout = pathlib.Path(tmp_dir) / "shadow"
            shadow_checkout.mkdir()
            prev_checkout = set_checkout(shadow_checkout)
            shadow_ready = False
            try:
                create_shadow_checkout(
                    real_checkout,
                    shadow_checkout,
                    modules,
                    zephyr_base,
                    src_override_dirs,
                    patches_dirs,
                )
                if func_name == "test":
                    _copy_build_metadata_to_shadow(
                        real_checkout, shadow_checkout, ec_rel_path
                    )
                with CodebasePreparer(
                    shadow_checkout,
                    patches_dirs,
                    src_override_dirs,
                    user_dirty_files,
                    commit=True,
                ):
                    os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                    os.environ["CROS_WORKON_SRCROOT"] = str(shadow_checkout)
                    shadow_ready = True
                    yield shadow_checkout / ec_rel_path
            finally:
                if shadow_ready:
                    try:
                        copy_outputs_back(
                            shadow_checkout, real_checkout, ec_rel_path
                        )
                    except OSError as e:
                        logger.error("Failed to copy outputs back: %s", e)
                _restore_env()
                set_checkout(prev_checkout)
    else:
        try:
            with CodebasePreparer(
                real_checkout, patches_dirs, src_override_dirs
            ):
                os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                yield None
        finally:
            _restore_env()


def main(argv=None):
    """CLI entry point for standalone codebase preparation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=pathlib.Path)
    parser.add_argument(
        "--patches-dir", action="append", dest="patches_dirs", type=pathlib.Path
    )
    parser.add_argument(
        "--src-override-dir",
        action="append",
        dest="src_override_dirs",
        type=pathlib.Path,
    )
    parser.add_argument("--commit", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    opts = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    script_path = pathlib.Path(__file__).resolve()
    ec_root = next(
        (
            p
            for p in script_path.parents
            if (p / "Makefile.rules").exists()
            or (p / "upstream_overrides").exists()
        ),
        script_path.parent,
    )
    checkout = opts.checkout
    if not checkout:
        try:
            checkout = find_checkout()
        except FileNotFoundError:
            checkout = (
                ec_root.parent.parent
                if (ec_root.parent.parent / "platform").is_dir()
                else ec_root
            )
    checkout = checkout.resolve()
    has_src = (checkout / "src").is_dir()
    patches_dirs = opts.patches_dirs or (
        get_default_patches_dirs(checkout)
        if has_src
        else [
            ec_root / "upstream_overrides" / "patches",
            ec_root.parent / "ec-private" / "patches",
        ]
    )
    src_override_dirs = opts.src_override_dirs or (
        get_default_src_override_dirs(checkout)
        if has_src
        else [
            ec_root / "upstream_overrides" / "src-override",
            ec_root.parent / "ec-private" / "src-override",
        ]
    )
    CodebasePreparer(
        checkout,
        patches_dirs,
        src_override_dirs,
        commit=opts.commit,
        allow_dirty=opts.allow_dirty,
    ).prepare()
    with contextlib.suppress(OSError):
        (checkout / ".workspace_prepared").touch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
