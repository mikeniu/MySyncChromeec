# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions to apply patches and source overrides to the checkout."""

import argparse
import collections
import contextlib
import logging
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import zmake.modules


logger = logging.getLogger("zmake.repository_prep")


class RepositoryPrepError(RuntimeError):
    """Exception raised when repository preparation fails."""


def _fail(msg, *args):
    """Log an error message and raise RepositoryPrepError."""
    logger.error(msg, *args)
    raise RepositoryPrepError(msg % args if args else msg)


def _git(cwd, *args, check=True, **kwargs):
    """Run a git command in cwd with captured output."""
    return subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, check=check, **kwargs
    )


_CONFIG = {"checkout_path": None}
_SHADOW_GIT_ENV = {
    "GIT_AUTHOR_NAME": "Shadow Builder",
    "GIT_AUTHOR_EMAIL": "shadow@builder.org",
    "GIT_COMMITTER_NAME": "Shadow Builder",
    "GIT_COMMITTER_EMAIL": "shadow@builder.org",
}
_PrepOpts = collections.namedtuple(
    "_PrepOpts", ["dirty_files", "commit", "allow_dirty", "path_mappings"]
)


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


def _get_default_dirs(checkout_root, subdir):
    """Get the default list of patches or source override directories."""
    platform_dir = pathlib.Path(checkout_root) / "src" / "platform"
    return [
        platform_dir / "ec" / "upstream_overrides" / subdir,
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


def get_dirty_files(repo_dir, checkout_root, path_mappings=None):
    """Get set of dirty/untracked files in a repo relative to checkout root."""
    repo_path = pathlib.Path(repo_dir)
    checkout_path = pathlib.Path(checkout_root).resolve()
    result = _git(
        repo_path,
        "--no-optional-locks",
        "status",
        "--porcelain",
        "-uall",
        "-z",
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
            try:
                dirty_files.add(abs_path.relative_to(checkout_path))
            except ValueError:
                for pfx, mapped in (path_mappings or {}).items():
                    mp = pathlib.Path(mapped).resolve()
                    with contextlib.suppress(ValueError):
                        dirty_files.add(
                            pathlib.Path(pfx) / abs_path.relative_to(mp)
                        )
    return dirty_files


def _resolve_target_path(checkout_root, rel_path, path_mappings=None):
    """Resolve a checkout-relative path against checkout_root."""
    root_path = pathlib.Path(checkout_root).resolve()
    rel = pathlib.Path(rel_path)
    if path_mappings:
        for prefix in sorted(
            path_mappings, key=lambda p: len(p.parts), reverse=True
        ):
            if rel.is_relative_to(prefix):
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
            if rel.is_relative_to(prefix):
                return mapped_base / rel.relative_to(prefix)

    # Support running directly inside the EC repository root
    expected_repo = "ec" if (root_path / "zephyr").is_dir() else "ec-legacy"
    if (
        len(rel.parts) >= 3
        and rel.parts[:3] == ("src", "platform", expected_repo)
        and (root_path / "Makefile").exists()
    ):
        return root_path / pathlib.Path(*rel.parts[3:])

    return direct


def find_target_repos(
    checkout_root, src_override_dirs, patches_dirs, path_mappings=None
):
    """Find all git repositories that are targets of overrides or patches."""
    repos = set()
    root_path = pathlib.Path(checkout_root).resolve()

    for override_dir in src_override_dirs or []:
        ovr_path = pathlib.Path(override_dir).resolve()
        for src in _find_override_files(ovr_path) if ovr_path.exists() else ():
            rel = src.relative_to(ovr_path)
            repo = find_git_repo(
                _resolve_target_path(root_path, rel, path_mappings)
            )
            if repo:
                repos.add(repo)

    for patches_dir in patches_dirs or []:
        p_path = pathlib.Path(patches_dir).resolve()
        for pf in _find_patch_files(p_path) if p_path.exists() else ():
            rel_parent = pf.relative_to(p_path).parent
            repo = _resolve_target_path(
                root_path, rel_parent, path_mappings
            ).resolve()
            repo_git = find_git_repo(repo)
            if repo_git:
                repos.add(repo_git)
            if repo.exists():
                with contextlib.suppress(subprocess.CalledProcessError):
                    for mod_file in get_patch_modified_files(pf, repo):
                        mod_git = find_git_repo(repo / mod_file)
                        if mod_git:
                            repos.add(mod_git)
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


def _has_files(path):
    """Check if path exists and contains non-ignored override files."""
    return bool(path) and any(_find_override_files(pathlib.Path(path)))


def _has_patch_files(path):
    """Check if path exists and contains patch files."""
    return bool(path) and any(_find_patch_files(pathlib.Path(path)))


def get_patch_modified_files(patch_file, target_dir):
    """Get list of files modified by a patch file, relative to target_dir."""
    res = _git(
        target_dir, "apply", "--numstat", "-z", str(patch_file), text=True
    )
    repo_root = find_git_repo(target_dir)
    prefix = None
    if repo_root:
        t_res = pathlib.Path(target_dir).resolve()
        with contextlib.suppress(ValueError):
            prefix = t_res.relative_to(repo_root.resolve())
    files, seen = [], set()
    for record in res.stdout.split("\0"):
        parts = record.split("\t", 2)
        if len(parts) >= 3 and parts[2]:
            p = pathlib.Path(parts[2])
            if prefix and str(prefix) != "." and p.is_relative_to(prefix):
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
        *,
        commit=False,
        path_mappings=None,
        allow_dirty=False,
    ):
        self.checkout_root = pathlib.Path(checkout_root).resolve()
        self.patches_dirs = [
            pathlib.Path(d).resolve() for d in (patches_dirs or [])
        ]
        self.src_override_dirs = [
            pathlib.Path(d).resolve() for d in (src_override_dirs or [])
        ]
        self.applied_patches = []
        self.copied_files = []
        self.modified_by_prep = set()
        self.opts = _PrepOpts(
            user_dirty_files,
            commit,
            allow_dirty,
            {
                pathlib.Path(k): pathlib.Path(v).resolve()
                for k, v in (path_mappings or {}).items()
            },
        )

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
            self.checkout_root, rel_path, self.opts.path_mappings
        )

    def _check_file_dirty(self, target_file, rel_path):
        """Check if a file is dirty in user workspace."""
        if self.opts.allow_dirty:
            return
        lexical_target = target_file.parent.resolve() / target_file.name
        if lexical_target in self.modified_by_prep:
            return
        if self.opts.dirty_files is not None:
            rel_candidates = {rel_path}
            with contextlib.suppress(ValueError):
                rel_candidates.add(
                    lexical_target.relative_to(self.checkout_root)
                )
            if rel_candidates & self.opts.dirty_files:
                _fail(
                    "File %s has local changes or is untracked "
                    "(from dirty list). Aborting to prevent data loss.",
                    target_file,
                )
        else:
            repo = find_git_repo(target_file.parent)
            if repo:
                rel_in_repo = lexical_target.relative_to(repo.resolve())
                status_out = _git(
                    repo,
                    "--no-optional-locks",
                    "status",
                    "--porcelain",
                    "--",
                    str(rel_in_repo),
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
        if self.opts.commit:
            self.commit_changes()

    def restore(self):
        """Revert patches and overrides in reverse order."""
        if self.opts.commit:
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
            self.opts.path_mappings,
        )
        allowed_roots = {self.checkout_root, *self.opts.path_mappings.values()}
        for repo in target_repos:
            if (
                repo.exists()
                and not repo.is_symlink()
                and not (repo / ".git").is_symlink()
                and any(repo.is_relative_to(r) for r in allowed_roots)
            ):
                logger.info("Committing applied changes in %s", repo)
                try:
                    _git(repo, "add", "-A", text=True)
                    if _git(
                        repo, "status", "--porcelain", text=True
                    ).stdout.strip():
                        _git(
                            repo,
                            "commit",
                            "-m",
                            "applied patches and overrides",
                            text=True,
                            env={**os.environ, **_SHADOW_GIT_ENV},
                        )
                except subprocess.CalledProcessError as e:
                    _fail(
                        "Failed to commit changes in %s: %s\nstderr: %s",
                        repo,
                        e,
                        e.stderr or "",
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
                existed = target_file.exists() or target_file.is_symlink()
                lex_tgt = target_file.parent.resolve() / target_file.name
                if existed:
                    self._check_file_dirty(target_file, rel_path)
                    if (
                        find_git_repo(target_file.parent)
                        and lex_tgt not in self.modified_by_prep
                        and not self.opts.allow_dirty
                    ):
                        res = _git(
                            target_file.parent,
                            "ls-files",
                            "--error-unmatch",
                            "--",
                            target_file.name,
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
                    if lex_tgt not in self.modified_by_prep:
                        self.copied_files.append((target_file, existed))
                        self.modified_by_prep.add(lex_tgt)
                except OSError as e:
                    _fail(
                        "Failed to copy %s to %s: %s", src_file, target_file, e
                    )

    def apply_patches(self):
        """Apply patches recursively from patches_dirs to the checkout."""
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
                can_apply = _git(
                    target_dir, "apply", "--check", str(patch_file), check=False
                )
                if (
                    can_apply.returncode != 0
                    and _git(
                        target_dir,
                        "apply",
                        "-R",
                        "--check",
                        str(patch_file),
                        check=False,
                    ).returncode
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
                        self.modified_by_prep.update(abs_mod_files)
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
        stop_roots = {self.checkout_root, *self.opts.path_mappings.values()}
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
                except OSError as e:
                    logger.error("Failed to remove %s: %s", target_file, e)
                    continue
                with contextlib.suppress(OSError):
                    parent = target_file.parent.resolve()
                    while (
                        parent not in stop_roots
                        and any(parent.is_relative_to(r) for r in stop_roots)
                        and not os.listdir(parent)
                    ):
                        os.rmdir(parent)
                        parent = parent.parent

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
        re.escape(old_str) + r"(?=" + re.escape(os.sep) + r"|$)", re.MULTILINE
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


def _copy_build_metadata_to_shadow(real_co, shadow_co, ec_rel):
    """Copy .shard_info.json and *.info from real build dir to shadow build dir."""
    real_build = real_co / ec_rel / "build"
    shadow_build = shadow_co / ec_rel / "build"
    if not real_build.exists():
        return
    for f in [
        real_build / "zephyr" / ".shard_info.json",
        *real_build.rglob("*.info"),
    ]:
        if f.is_file() and not f.is_symlink():
            dst = shadow_build / f.relative_to(real_build)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dst)
    fix_lcov_info_paths(shadow_build, real_co, shadow_co)


def copy_outputs_back(shadow_co, real_co, ec_rel):
    """Copy build and twister outputs from shadow back to real checkout."""
    shadow_ec, real_ec = shadow_co / ec_rel, real_co / ec_rel
    for src_path in [shadow_ec / "build", *shadow_ec.glob("twister-out*")]:
        if not src_path.exists() or src_path.is_symlink():
            continue
        dst_path = real_ec / src_path.name
        logger.info("Copying %s back to real checkout...", src_path.name)
        if src_path.is_dir():
            _copytree_overwrite(src_path, dst_path)
            fix_symlinks(dst_path, shadow_co, real_co)
            fix_lcov_info_paths(dst_path, shadow_co, real_co)
        else:
            shutil.copy2(src_path, dst_path)


def _needs_copy_for_overrides(rel_path, src_override_dirs):
    """Check if the path has files in overrides."""
    return any(
        _has_files(pathlib.Path(d) / rel_path) for d in src_override_dirs
    )


def needs_copy_for_patches(rel_path, patches_dirs):
    """Check if the path (or an ancestor git repo) has patches targeting it."""
    rel = pathlib.Path(rel_path)
    for p_dir in (
        pathlib.Path(d) for d in patches_dirs if pathlib.Path(d).exists()
    ):
        if _has_patch_files(p_dir / rel):
            return True
        for ancestor in (a for a in rel.parents if len(a.parts) >= 3):
            anc_dir = p_dir / ancestor
            sub_prefix = f"{rel.relative_to(ancestor).as_posix()}/"
            for pf in anc_dir.glob("*.patch") if anc_dir.is_dir() else ():
                try:
                    content = pf.read_text(encoding="utf-8", errors="ignore")
                except OSError:
                    return True
                if f"a/{sub_prefix}" in content or f"b/{sub_prefix}" in content:
                    return True
    return False


def _shadow_copy_ignore(repo_root):
    """Return copytree ignore function skipping .git and root build dirs."""
    root_str = str(repo_root)

    def _ignore(cur_dir, names):
        ignored = {".git"} if ".git" in names else set()
        if cur_dir == root_str:
            ignored.update(
                n for n in names if n == "build" or n.startswith("twister-out")
            )
        return ignored

    return _ignore


def create_shadow_checkout(
    real_checkout,
    shadow_root,
    modules,
    zephyr_base,
    src_override_dirs,
    patches_dirs,
):
    """Create a shadow checkout with copies of targets and symlinks."""
    real_root = pathlib.Path(real_checkout).resolve()
    shadow_path = pathlib.Path(shadow_root).resolve()
    (shadow_path / ".repo").mkdir(parents=True, exist_ok=True)
    git_env = {**os.environ, **_SHADOW_GIT_ENV}

    tp_dir = real_root / "src" / "third_party"
    # Third-party repositories consumed by EC and FPMCU builds (e.g. crypto,
    # linear algebra, and unit-test libraries) that may be targeted by patches
    # or source overrides.
    paths_to_handle = {
        "zephyr_base": pathlib.Path(zephyr_base),
        "ec_private": real_root / "src" / "platform" / "ec-private",
        "chromiumos_overlay": tp_dir / "chromiumos-overlay",
        **{f"module:{k}": pathlib.Path(v) for k, v in modules.items()},
        **{
            n: tp_dir / n
            for n in ("boringssl", "cryptoc", "eigen3", "googletest", "tpm2")
        },
        **{
            n: real_root / "src" / n
            for n in ("project", "overlays", "private-overlays")
        },
    }
    fp_dir = real_root / "src" / "platform" / "fingerprint"
    if fp_dir.is_dir():
        paths_to_handle.update(
            (f"fp:{s.name}", s)
            for s in sorted(fp_dir.iterdir())
            if s.is_dir() and not s.name.startswith(".")
        )

    seen_rel_paths, copied_rel_paths = set(), set()
    for key, path in paths_to_handle.items():
        if not path.exists():
            continue
        try:
            rel_path = path.resolve().relative_to(real_root)
        except ValueError:
            if key != "zephyr_base":
                continue
            rel_path = pathlib.Path("src/third_party/zephyrproject/zephyr")
        if rel_path in seen_rel_paths:
            continue
        seen_rel_paths.add(rel_path)
        dst_path = shadow_path / rel_path
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        if (
            key == "module:ec"
            or _needs_copy_for_overrides(rel_path, src_override_dirs)
            or needs_copy_for_patches(rel_path, patches_dirs)
        ):
            copied_rel_paths.add(rel_path)
            logger.info("Copying %s to shadow checkout...", rel_path)
            shutil.copytree(
                path, dst_path, ignore=_shadow_copy_ignore(path), symlinks=True
            )
            for args in (
                ("init",),
                ("config", "core.abbrev", "10"),
                ("add", "-A"),
                ("commit", "-m", "initial"),
            ):
                _git(dst_path, *args, env=git_env)
        else:
            logger.debug("Symlinking %s to shadow checkout...", rel_path)
            os.symlink(path, dst_path)

    zp_rel = pathlib.Path("src/third_party/zephyrproject")
    zp_touched = (
        not pathlib.Path(zephyr_base).resolve().is_relative_to(real_root)
        or any(rel.is_relative_to(zp_rel) for rel in copied_rel_paths)
        or _needs_copy_for_overrides(zp_rel, src_override_dirs)
        or needs_copy_for_patches(zp_rel, patches_dirs)
    )
    real_zp_git, shadow_zp = real_root / zp_rel / ".git", shadow_path / zp_rel
    if (
        not zp_touched
        and real_zp_git.exists()
        and shadow_zp.is_dir()
        and not (shadow_zp / ".git").exists()
    ):
        os.symlink(real_zp_git.resolve(), shadow_zp / ".git")


def resolve_prep_dirs(checkout_root, opts):
    """Return (patches_dirs, src_override_dirs, has_work) for checkout_root."""
    p_dirs = getattr(opts, "patches_dirs", None) or get_default_patches_dirs(
        checkout_root
    )
    o_dirs = getattr(
        opts, "src_override_dirs", None
    ) or get_default_src_override_dirs(checkout_root)
    has_work = any(_has_files(d) for d in o_dirs) or any(
        _has_patch_files(d) for d in p_dirs
    )
    return p_dirs, o_dirs, has_work


def _remap_to_shadow(path, real_co, shadow_co, default_rel=None):
    """Remap a path under real_co to shadow_co, or fallback to default_rel."""
    try:
        return shadow_co / pathlib.Path(path).resolve().relative_to(real_co)
    except ValueError:
        return shadow_co / default_rel if default_rel else path


def _remap_shadow_opts(opts, real_co, shadow_co, zp_rel):
    """Remap CLI path options on opts into shadow_co."""
    opts.checkout = shadow_co
    if getattr(opts, "zephyr_base", None):
        opts.zephyr_base = _remap_to_shadow(
            opts.zephyr_base, real_co, shadow_co, zp_rel
        )
    if getattr(opts, "projects_dirs", None):
        opts.projects_dirs = [
            _remap_to_shadow(p, real_co, shadow_co) for p in opts.projects_dirs
        ]
    if getattr(opts, "modules_dir", None):
        opts.modules_dir = _remap_to_shadow(
            opts.modules_dir, real_co, shadow_co
        )


@contextlib.contextmanager
def prepared_workspace(opts, modules=None, checkout=None):
    """Context manager to prepare a shadow or in-place workspace."""
    func_name = getattr(getattr(opts, "func", None), "__name__", "")
    real_co = pathlib.Path(checkout or find_checkout()).resolve()
    p_dirs, o_dirs, has_work = resolve_prep_dirs(real_co, opts)
    use_shadow = (
        has_work
        and not getattr(opts, "no_shadow", False)
        and getattr(opts, "subcommand", None) != "compare-builds"
        and func_name not in ("bundle", "merge_shards")
    )
    env_keys = (
        "ZMAKE_WORKSPACE_PREPARED",
        "CROS_WORKON_SRCROOT",
        "ZMAKE_ORIGIN_CHECKOUT",
    )
    saved_env = {k: os.environ.get(k) for k in env_keys}
    try:
        if not use_shadow:
            with CodebasePreparer(real_co, p_dirs, o_dirs):
                os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                yield None
            return

        logger.info("Using shadow workspace for build...")
        zp_rel = pathlib.Path("src/third_party/zephyrproject/zephyr")
        zb = pathlib.Path(
            getattr(opts, "zephyr_base", None) or (real_co / zp_rel)
        )
        res_zb = zb.resolve()
        ext_map = (
            {zp_rel: res_zb} if not res_zb.is_relative_to(real_co) else None
        )
        repos = find_target_repos(real_co, o_dirs, p_dirs, ext_map)
        dirty = {
            f for repo in repos for f in get_dirty_files(repo, real_co, ext_map)
        }
        modules = modules or zmake.modules.locate_from_checkout(real_co)
        ec_rel = modules["ec"].resolve().relative_to(real_co)

        with tempfile.TemporaryDirectory() as tmp_dir:
            shadow_co = pathlib.Path(tmp_dir) / "shadow"
            shadow_co.mkdir()
            prev_co = set_checkout(shadow_co)
            shadow_ready = False
            try:
                create_shadow_checkout(
                    real_co, shadow_co, modules, zb, o_dirs, p_dirs
                )
                if func_name == "test":
                    _copy_build_metadata_to_shadow(real_co, shadow_co, ec_rel)
                _remap_shadow_opts(opts, real_co, shadow_co, zp_rel)
                with CodebasePreparer(
                    shadow_co, p_dirs, o_dirs, dirty, commit=True
                ):
                    os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                    os.environ["CROS_WORKON_SRCROOT"] = str(shadow_co)
                    os.environ["ZMAKE_ORIGIN_CHECKOUT"] = str(real_co)
                    shadow_ready = True
                    yield shadow_co / ec_rel
            finally:
                if shadow_ready:
                    try:
                        copy_outputs_back(shadow_co, real_co, ec_rel)
                    except OSError as e:
                        logger.error("Failed to copy outputs back: %s", e)
                set_checkout(prev_co)
    finally:
        for k, v in saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def main(argv=None):
    """CLI entry point for standalone codebase preparation."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkout", type=pathlib.Path)
    for flag, dst in (
        ("--patches-dir", "patches_dirs"),
        ("--src-override-dir", "src_override_dirs"),
    ):
        parser.add_argument(flag, action="append", dest=dst, type=pathlib.Path)
    parser.add_argument("--commit", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    opts = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    ec_root = pathlib.Path(__file__).resolve().parents[3]
    raw_checkout = opts.checkout
    if not raw_checkout:
        with contextlib.suppress(FileNotFoundError):
            raw_checkout = find_checkout()
    if not raw_checkout:
        up = ec_root.parent.parent
        raw_checkout = up if (up / "platform").is_dir() else ec_root
    checkout = raw_checkout.resolve()
    has_src = (checkout / "src").is_dir()
    ovr_bases = [ec_root / "upstream_overrides", ec_root.parent / "ec-private"]
    p_dirs = opts.patches_dirs or (
        get_default_patches_dirs(checkout)
        if has_src
        else [b / "patches" for b in ovr_bases]
    )
    o_dirs = opts.src_override_dirs or (
        get_default_src_override_dirs(checkout)
        if has_src
        else [b / "src-override" for b in ovr_bases]
    )
    try:
        CodebasePreparer(
            checkout,
            p_dirs,
            o_dirs,
            commit=opts.commit,
            allow_dirty=opts.allow_dirty,
        ).prepare()
    except RepositoryPrepError:
        return 1
    with contextlib.suppress(OSError):
        (checkout / ".workspace_prepared").touch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
