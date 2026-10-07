#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions to apply patches and source overrides to the checkout."""

import argparse
import contextlib
import logging
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


logger = logging.getLogger("repository_prep")


class RepositoryPrepError(RuntimeError):
    """Exception raised when repository preparation fails."""


def _fail(msg, *args):
    """Log an error message and raise RepositoryPrepError."""
    logger.error(msg, *args)
    raise RepositoryPrepError(msg % args if args else msg)


_CONFIG = {"checkout_path": None}

_SHADOW_GIT_ENV = {
    "GIT_AUTHOR_NAME": "Shadow Builder",
    "GIT_AUTHOR_EMAIL": "shadow@builder.org",
    "GIT_COMMITTER_NAME": "Shadow Builder",
    "GIT_COMMITTER_EMAIL": "shadow@builder.org",
}


def set_checkout(path):
    """Override the checkout path to be used by find_checkout."""
    old_path = _CONFIG["checkout_path"]
    _CONFIG["checkout_path"] = None if path is None else pathlib.Path(path)
    return old_path


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    if _CONFIG["checkout_path"]:
        return _CONFIG["checkout_path"]
    for path in pathlib.Path(__file__).resolve().parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


def _get_ec_repo_name(checkout_root):
    """Determine the EC repository directory name under src/platform."""
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    if repo_root.name in ("ec", "ec-legacy"):
        return repo_root.name
    platform = pathlib.Path(checkout_root) / "src" / "platform"
    if not (platform / "ec").exists() and (platform / "ec-legacy").exists():
        return "ec-legacy"
    return "ec"


def _get_default_dirs(checkout_root, subdir):
    """Get the default list of patches or source override directories."""
    platform_dir = pathlib.Path(checkout_root) / "src" / "platform"
    ec_name = _get_ec_repo_name(checkout_root)
    return [
        platform_dir / ec_name / "upstream_overrides" / subdir,
        platform_dir / "ec-private" / subdir,
    ]


def get_default_patches_dirs(checkout_root):
    """Get the default list of patches directories."""
    return _get_default_dirs(checkout_root, "patches")


def get_default_src_override_dirs(checkout_root):
    """Get the default list of source override directories."""
    return _get_default_dirs(checkout_root, "src-override")


def find_git_repo(path):
    """Find the git repository root for a given path."""
    p = pathlib.Path(path)
    cur = (
        p.parent.resolve() / p.name
        if p.is_symlink() and not p.is_dir()
        else p.resolve()
    )
    return next((d for d in (cur, *cur.parents) if (d / ".git").exists()), None)


def get_dirty_files(repo_dir, checkout_root):
    """Get list of dirty/untracked files in a repo, relative to checkout."""
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


def _resolve_target_path(checkout_root, rel_path, path_mappings=None):
    """Resolve a checkout-relative path against checkout_root."""
    root_path = pathlib.Path(checkout_root).resolve()
    rel = pathlib.Path(rel_path)
    if path_mappings:
        for prefix in sorted(
            path_mappings, key=lambda p: len(p.parts), reverse=True
        ):
            if rel == prefix or prefix in rel.parents:
                return path_mappings[prefix] / rel.relative_to(prefix)

    direct = root_path / rel
    if direct.exists() or (root_path / "src").exists():
        return direct

    # Support ebuild WORKDIR layouts without top-level 'src/' directory
    if rel.parts and rel.parts[0] == "src":
        without_src = pathlib.Path(*rel.parts[1:])
        candidate = root_path / without_src
        if candidate.exists() or (
            without_src.parts and (root_path / without_src.parts[0]).exists()
        ):
            return candidate

    # Support chromeos-zephyr ebuild layout (${S}/zephyrproject/...)
    zephyr_dir = root_path / "zephyrproject"
    if zephyr_dir.is_dir():
        mod_dir = zephyr_dir / "modules"
        zephyr_mappings = {
            pathlib.Path("src/platform/ec"): mod_dir / "ec",
            pathlib.Path("src/third_party/zephyrproject"): zephyr_dir,
            pathlib.Path("src/third_party/pigweed"): mod_dir / "pigweed",
            pathlib.Path("src/third_party/zephyr"): mod_dir,
        }
        for prefix, mapped_base in zephyr_mappings.items():
            if rel == prefix or prefix in rel.parents:
                sub_rel = rel.relative_to(prefix)
                return (
                    mapped_base
                    if str(sub_rel) == "."
                    else mapped_base / sub_rel
                )

    # Support running directly inside the EC repository root
    expected_repo = "ec" if (root_path / "zephyr").is_dir() else "ec-legacy"
    if (
        len(rel.parts) >= 3
        and rel.parts[:3] == ("src", "platform", expected_repo)
        and (root_path / "Makefile").exists()
    ):
        sub_rel = pathlib.Path(*rel.parts[3:])
        return root_path if str(sub_rel) == "." else root_path / sub_rel

    return direct


def find_target_repos(
    checkout_root, src_override_dirs, patches_dirs, path_mappings=None
):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    root_path = pathlib.Path(checkout_root).resolve()

    for override_dir in src_override_dirs:
        override_path = pathlib.Path(override_dir).resolve()
        if not override_path.exists():
            continue
        for src_file in _find_override_files(override_path):
            rel_path = src_file.relative_to(override_path)
            real_path = _resolve_target_path(root_path, rel_path, path_mappings)
            repo = find_git_repo(real_path)
            if repo:
                repos.add(repo)

    for patches_dir in patches_dirs:
        patches_path = pathlib.Path(patches_dir).resolve()
        if not patches_path.exists():
            continue
        for patch_file in _find_patch_files(patches_path):
            rel_path = patch_file.relative_to(patches_path)
            repo = _resolve_target_path(
                root_path, rel_path.parent, path_mappings
            ).resolve()
            repo_git = find_git_repo(repo)
            if repo_git:
                repos.add(repo_git)
            if repo.exists():
                with contextlib.suppress(subprocess.CalledProcessError):
                    for mod_file in get_patch_modified_files(patch_file, repo):
                        mod_git = find_git_repo(repo / mod_file)
                        if mod_git:
                            repos.add(mod_git)
    return repos


def find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, dirs, files in os.walk(patches_dir):
        if ".git" in dirs:
            dirs.remove(".git")
        for file in files:
            if file.endswith(".patch"):
                yield pathlib.Path(root) / file


_find_patch_files = find_patch_files


def find_override_files(src_override_dir):
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


_find_override_files = find_override_files


def get_patch_modified_files(patch_file, target_dir):
    """Get list of files modified by a patch file, relative to target_dir."""
    res = subprocess.run(
        ["git", "apply", "--numstat", "-z", str(patch_file)],
        cwd=target_dir,
        capture_output=True,
        check=True,
        text=True,
    )
    repo_root = find_git_repo(target_dir)
    prefix = None
    if repo_root:
        with contextlib.suppress(ValueError):
            prefix = (
                pathlib.Path(target_dir)
                .resolve()
                .relative_to(repo_root.resolve())
            )
    files = []
    seen = set()
    for record in res.stdout.split("\0"):
        parts = record.split("\t", 2)
        if len(parts) >= 3 and parts[2]:
            p = pathlib.Path(parts[2])
            if (
                prefix
                and str(prefix) != "."
                and (p == prefix or prefix in p.parents)
            ):
                p = p.relative_to(prefix)
            if p not in seen:
                seen.add(p)
                files.append(p)
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
        self.patches_dirs = [pathlib.Path(d).resolve() for d in patches_dirs]
        self.src_override_dirs = [
            pathlib.Path(d).resolve() for d in src_override_dirs
        ]
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
        try:
            self.prepare()
        except BaseException:
            self.restore()
            raise
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
        lexical_target = target_file.parent.resolve() / target_file.name
        if lexical_target in self._state["modified_by_prep"]:
            return
        user_dirty_files = self._state["user_dirty_files"]
        if user_dirty_files is not None:
            rel_candidates = {rel_path}
            with contextlib.suppress(ValueError):
                rel_candidates.add(
                    lexical_target.relative_to(self.checkout_root)
                )
            if rel_candidates & user_dirty_files:
                _fail(
                    "File %s has local changes or is untracked "
                    "(from dirty list). Aborting to prevent data loss.",
                    target_file,
                )
        else:
            repo = find_git_repo(target_file.parent)
            if repo:
                rel_in_repo = lexical_target.relative_to(repo.resolve())
                status_out = subprocess.run(
                    ["git", "status", "--porcelain", "--", str(rel_in_repo)],
                    cwd=repo,
                    capture_output=True,
                    check=False,
                    text=True,
                ).stdout.strip()
                if status_out:
                    _fail(
                        "File %s has local changes (%s). Aborting.",
                        target_file,
                        status_out,
                    )

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

        def _git_run(repo, *args, env=None):
            return subprocess.run(
                ["git", *args],
                cwd=repo,
                capture_output=True,
                check=True,
                text=True,
                env=env,
            )

        for repo in target_repos:
            if (
                not repo.exists()
                or repo.is_symlink()
                or (repo / ".git").is_symlink()
            ):
                continue
            logger.info("Committing applied changes in %s", repo)
            try:
                _git_run(repo, "add", "-A")
                if _git_run(repo, "status", "--porcelain").stdout.strip():
                    _git_run(
                        repo,
                        "commit",
                        "-m",
                        "applied patches and overrides",
                        env={**os.environ, **_SHADOW_GIT_ENV},
                    )
            except subprocess.CalledProcessError as e:
                stderr = (
                    e.stderr.decode(errors="replace")
                    if isinstance(e.stderr, bytes)
                    else (e.stderr or "")
                )
                _fail(
                    "Failed to commit changes in %s: %s\nstderr: %s",
                    repo,
                    e,
                    stderr,
                )

    def copy_source_overrides(self):
        """Copy source overrides to the checkout."""
        for src_override_dir in self.src_override_dirs:
            if not src_override_dir.exists():
                continue

            logger.info("Copying files from %s", src_override_dir)
            for src_file in sorted(_find_override_files(src_override_dir)):
                rel_path = src_file.relative_to(src_override_dir)
                target_file = self._resolve_target_path(rel_path)

                existed_before = (
                    target_file.exists() or target_file.is_symlink()
                )
                if existed_before:
                    self._check_file_dirty(target_file, rel_path)
                    lexical_target = (
                        target_file.parent.resolve() / target_file.name
                    )
                    if (
                        find_git_repo(target_file.parent)
                        and lexical_target
                        not in self._state["modified_by_prep"]
                        and not self._state["allow_dirty"]
                    ):
                        ls_cmd = [
                            "git",
                            "ls-files",
                            "--error-unmatch",
                            "--",
                            target_file.name,
                        ]
                        res = subprocess.run(
                            ls_cmd,
                            cwd=target_file.parent,
                            capture_output=True,
                            check=False,
                        )
                        if res.returncode != 0:
                            _fail(
                                "File %s is untracked. Aborting.", target_file
                            )

                target_file.parent.mkdir(parents=True, exist_ok=True)

                logger.info("Copying %s to %s", src_file, target_file)
                try:
                    if target_file.is_symlink():
                        target_file.unlink()
                    shutil.copy2(src_file, target_file)
                    resolved_target = (
                        target_file.parent.resolve() / target_file.name
                    )
                    if resolved_target not in self._state["modified_by_prep"]:
                        self.copied_files.append((target_file, existed_before))
                        self._state["modified_by_prep"].add(resolved_target)
                except OSError as e:
                    _fail(
                        "Failed to copy %s to %s: %s", src_file, target_file, e
                    )

    def apply_patches(self):
        """Apply patches recursively from patches_dirs to the checkout."""

        def _check_apply(cwd, pf, *extra):
            return subprocess.run(
                ["git", "apply", *extra, "--check", str(pf)],
                cwd=cwd,
                capture_output=True,
                check=False,
            )

        for patches_dir in self.patches_dirs:
            if not patches_dir.exists():
                continue

            logger.info("Applying patches from %s", patches_dir)
            for patch_file in sorted(_find_patch_files(patches_dir)):
                rel_path = patch_file.relative_to(patches_dir)
                target_rel_dir = rel_path.parent
                target_dir = self._resolve_target_path(target_rel_dir)

                if not target_dir.exists():
                    logger.warning(
                        "Target dir %s for %s missing. Skipping.",
                        target_dir,
                        patch_file,
                    )
                    continue

                logger.info(
                    "Processing patch %s for %s", patch_file, target_dir
                )
                can_apply = _check_apply(target_dir, patch_file)
                if (
                    can_apply.returncode != 0
                    and _check_apply(target_dir, patch_file, "-R").returncode
                    == 0
                ):
                    logger.info(
                        "Patch %s already applied. Skipping.", patch_file
                    )
                    continue

                abs_mod_files = []
                try:
                    for mod_file in get_patch_modified_files(
                        patch_file, target_dir
                    ):
                        mod_target = target_dir / mod_file
                        abs_mod = mod_target.parent.resolve() / mod_target.name
                        abs_mod_files.append(abs_mod)
                        self._check_file_dirty(
                            mod_target, target_rel_dir / mod_file
                        )
                except subprocess.CalledProcessError as e:
                    _fail("Failed to inspect patch %s: %s", patch_file, e)

                try:
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
                        _fail(
                            "Patch %s cannot be applied.\n"
                            "stdout: %s\nstderr: %s",
                            patch_file,
                            can_apply.stdout.decode(),
                            can_apply.stderr.decode(),
                        )
                except subprocess.CalledProcessError as e:
                    _fail("Failed to apply patch %s: %s", patch_file, e)

    def revert_source_overrides(self):
        """Restore or remove copied source overrides."""
        stop_roots = {
            self.checkout_root,
            *self._state["path_mappings"].values(),
        }
        for target_file, existed_before in reversed(self.copied_files):
            if existed_before:
                if find_git_repo(target_file.parent):
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
                    logger.warning(
                        "Cannot restore untracked/non-git file %s", target_file
                    )
            else:
                logger.info("Removing new file %s", target_file)
                try:
                    os.remove(target_file)
                    parent = target_file.parent
                    while (
                        parent not in stop_roots
                        and any(r in parent.parents for r in stop_roots)
                        and not os.listdir(parent)
                    ):
                        os.rmdir(parent)
                        parent = parent.parent
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


def _has_files(path):
    """Check if path exists and contains non-ignored override files."""
    p = pathlib.Path(path) if path else None
    return bool(p and p.exists() and any(_find_override_files(p)))


def _has_patch_files(path):
    """Check if path exists and contains patch files."""
    p = pathlib.Path(path) if path else None
    return bool(p and p.exists() and any(_find_patch_files(p)))


def fix_symlinks(directory, old_prefix, new_prefix):
    """Find and fix symlinks pointing to old_prefix to point to new_prefix."""
    old_str = str(old_prefix).rstrip(os.sep)
    new_str = str(new_prefix).rstrip(os.sep)
    for root, dirs, files in os.walk(directory):
        for d in dirs + files:
            path = pathlib.Path(root) / d
            if path.is_symlink():
                target = os.readlink(path)
                if target == old_str or target.startswith(old_str + os.sep):
                    new_target = new_str + target[len(old_str) :]
                    path.unlink()
                    os.symlink(new_target, path)


def _copytree_overwrite(src, dst):
    """Copy directory tree src to dst, overwriting existing files/symlinks."""
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        target = dst / item.name
        if target.is_symlink() or target.is_file():
            target.unlink()
        elif target.is_dir() and (item.is_symlink() or not item.is_dir()):
            shutil.rmtree(target)
        if item.is_symlink():
            os.symlink(os.readlink(item), target)
        elif item.is_dir():
            _copytree_overwrite(item, target)
        else:
            shutil.copy2(item, target)


def fix_lcov_info_paths(directory, old_prefix, new_prefix):
    """Replace old_prefix with new_prefix in *.info files under directory."""
    old_str = str(old_prefix).rstrip(os.sep)
    new_str = str(new_prefix).rstrip(os.sep)
    if not directory.exists() or old_str == new_str:
        return
    pattern = re.compile(
        re.escape(old_str) + r"(?=" + re.escape(os.sep) + r"|$)",
        re.MULTILINE,
    )
    for info_file in directory.rglob("*.info"):
        if info_file.is_symlink() or not info_file.is_file():
            continue
        try:
            stat = info_file.stat()
            content = info_file.read_text(encoding="utf-8", errors="ignore")
            new_content = pattern.sub(lambda _: new_str, content)
            if new_content != content:
                info_file.write_text(new_content, encoding="utf-8")
                os.utime(info_file, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        except OSError as e:
            logger.warning("Failed to update paths in %s: %s", info_file, e)


def copy_outputs_back(shadow_checkout, real_checkout, ec_rel_path):
    """Copy build outputs from shadow checkout back to real checkout."""
    shadow_build = shadow_checkout / ec_rel_path / "build"
    real_build = real_checkout / ec_rel_path / "build"
    if shadow_build.exists() and not shadow_build.is_symlink():
        logger.info("Copying build outputs back to real checkout...")
        _copytree_overwrite(shadow_build, real_build)
        fix_symlinks(real_build, shadow_checkout, real_checkout)
        fix_lcov_info_paths(real_build, shadow_checkout, real_checkout)


def _needs_copy_for_overrides(rel_path, src_override_dirs):
    """Check if the path has files in overrides."""
    return any(
        _has_files(pathlib.Path(d) / rel_path) for d in src_override_dirs
    )


def needs_copy_for_patches(rel_path, patches_dirs):
    """Check if the path (or an ancestor git repo) has patches targeting it."""
    rel = pathlib.Path(rel_path)
    for patches_dir in patches_dirs:
        patches_path = pathlib.Path(patches_dir)
        if not patches_path.exists():
            continue
        if _has_patch_files(patches_path / rel):
            return True
        for ancestor in rel.parents:
            if len(ancestor.parts) < 3:
                continue
            anc_dir = patches_path / ancestor
            if not anc_dir.is_dir():
                continue
            sub_prefix = f"{rel.relative_to(ancestor).as_posix()}/"
            for pf in anc_dir.glob("*.patch"):
                try:
                    content = pf.read_text(encoding="utf-8", errors="ignore")
                except OSError:
                    return True
                if f"a/{sub_prefix}" in content or f"b/{sub_prefix}" in content:
                    return True
    return False


_needs_copy_for_patches = needs_copy_for_patches


def _shadow_copy_ignore(repo_root):
    """Return copytree ignore function skipping .git and root build dir."""
    root_str = str(repo_root)

    def _ignore(cur_dir, names):
        ignored = {".git"} if ".git" in names else set()
        if cur_dir == root_str and "build" in names:
            ignored.add("build")
        return ignored

    return _ignore


def create_shadow_checkout(
    real_checkout, shadow_root, ec_path, src_override_dirs, patches_dirs
):
    """Create a shadow checkout with copies of targets and symlinks."""
    real_root = pathlib.Path(real_checkout).resolve()
    shadow_path = pathlib.Path(shadow_root).resolve()
    (shadow_path / ".repo").mkdir(parents=True, exist_ok=True)

    paths_to_handle = {
        "ec": pathlib.Path(ec_path),
        "ec_private": real_root / "src" / "platform" / "ec-private",
        "chromiumos_overlay": (
            real_root / "src" / "third_party" / "chromiumos-overlay"
        ),
    }
    # Third-party repositories consumed by EC and FPMCU builds (e.g. crypto,
    # linear algebra, and unit-test libraries) that may be targeted by patches
    # or source overrides.
    for name in ("boringssl", "cryptoc", "eigen3", "googletest", "tpm2"):
        paths_to_handle[name] = real_root / "src" / "third_party" / name
    for name in ("project", "overlays", "private-overlays"):
        paths_to_handle[name] = real_root / "src" / name
    fp_dir = real_root / "src" / "platform" / "fingerprint"
    if fp_dir.is_dir():
        for sub in sorted(fp_dir.iterdir()):
            if sub.is_dir() and not sub.name.startswith("."):
                paths_to_handle[f"fp:{sub.name}"] = sub

    seen_rel_paths = set()
    for key, path in paths_to_handle.items():
        if not path.exists():
            continue
        try:
            rel_path = path.resolve().relative_to(real_root)
        except ValueError:
            continue
        if rel_path in seen_rel_paths:
            continue
        seen_rel_paths.add(rel_path)
        dst_path = shadow_path / rel_path

        need_copy = (
            key == "ec"
            or _needs_copy_for_overrides(rel_path, src_override_dirs)
            or needs_copy_for_patches(rel_path, patches_dirs)
        )

        dst_path.parent.mkdir(parents=True, exist_ok=True)
        if need_copy:
            logger.info("Copying %s to shadow checkout...", rel_path)
            shutil.copytree(
                path,
                dst_path,
                ignore=_shadow_copy_ignore(path),
                symlinks=True,
            )
            git_env = {**os.environ, **_SHADOW_GIT_ENV}
            for cmd in (
                ["git", "init"],
                ["git", "config", "core.abbrev", "10"],
                ["git", "add", "-A"],
                ["git", "commit", "-m", "initial"],
            ):
                subprocess.run(
                    cmd,
                    cwd=dst_path,
                    capture_output=True,
                    check=True,
                    env=git_env,
                )
        else:
            logger.debug("Symlinking %s to shadow checkout...", rel_path)
            os.symlink(path, dst_path)


@contextlib.contextmanager
def prepared_workspace(opts, ec_dir=None):
    """Context manager to prepare a shadow or in-place workspace."""
    func_name = getattr(getattr(opts, "func", None), "__name__", "")
    if func_name == "merge_shards":
        yield None
        return
    real_checkout = find_checkout().resolve()
    patches_dirs = getattr(opts, "patches_dirs", None) or (
        get_default_patches_dirs(real_checkout)
    )
    src_override_dirs = getattr(opts, "src_override_dirs", None) or (
        get_default_src_override_dirs(real_checkout)
    )
    use_shadow = (
        ec_dir is not None
        and (
            any(_has_files(d) for d in src_override_dirs)
            or any(_has_patch_files(d) for d in patches_dirs)
        )
        and not getattr(opts, "no_shadow", False)
    )

    prev_srcroot = os.environ.get("CROS_WORKON_SRCROOT")

    def _restore_env():
        if prev_srcroot is None:
            os.environ.pop("CROS_WORKON_SRCROOT", None)
        else:
            os.environ["CROS_WORKON_SRCROOT"] = prev_srcroot

    if use_shadow:
        logger.info("Using shadow workspace for build...")
        target_repos = find_target_repos(
            real_checkout, src_override_dirs, patches_dirs
        )
        user_dirty_files = {
            f
            for repo in target_repos
            for f in get_dirty_files(repo, real_checkout)
        }
        ec_rel_path = pathlib.Path(ec_dir).resolve().relative_to(real_checkout)

        with tempfile.TemporaryDirectory() as tmp_dir:
            shadow_checkout = pathlib.Path(tmp_dir) / "shadow"
            shadow_checkout.mkdir()
            prev_checkout = set_checkout(shadow_checkout)
            shadow_ready = False
            try:
                create_shadow_checkout(
                    real_checkout,
                    shadow_checkout,
                    pathlib.Path(ec_dir),
                    src_override_dirs,
                    patches_dirs,
                )
                with CodebasePreparer(
                    shadow_checkout,
                    patches_dirs,
                    src_override_dirs,
                    user_dirty_files,
                    commit=True,
                ):
                    if (
                        func_name == "bundle"
                        and (real_checkout / ec_rel_path / "build").exists()
                    ):
                        os.symlink(
                            real_checkout / ec_rel_path / "build",
                            shadow_checkout / ec_rel_path / "build",
                        )
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

    repo_root = pathlib.Path(__file__).resolve().parent.parent
    if repo_root.name in ("ec", "ec-legacy"):
        ec_root = repo_root
    elif (repo_root.parent / "ec").is_dir():
        ec_root = repo_root.parent / "ec"
    else:
        ec_root = repo_root.parent / "ec-legacy"
    if opts.checkout is not None:
        raw_checkout = opts.checkout
    else:
        try:
            raw_checkout = find_checkout()
        except FileNotFoundError:
            parent_co = ec_root.parent.parent
            raw_checkout = (
                parent_co if (parent_co / "platform").is_dir() else ec_root
            )
    resolved_checkout = raw_checkout.resolve()
    has_src = (resolved_checkout / "src").is_dir()
    ovr_bases = [ec_root / "upstream_overrides", ec_root.parent / "ec-private"]
    patches_dirs = opts.patches_dirs or (
        get_default_patches_dirs(resolved_checkout)
        if has_src
        else [b / "patches" for b in ovr_bases]
    )
    src_override_dirs = opts.src_override_dirs or (
        get_default_src_override_dirs(resolved_checkout)
        if has_src
        else [b / "src-override" for b in ovr_bases]
    )
    try:
        CodebasePreparer(
            resolved_checkout,
            patches_dirs,
            src_override_dirs,
            commit=opts.commit,
            allow_dirty=opts.allow_dirty,
        ).prepare()
    except RepositoryPrepError:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
