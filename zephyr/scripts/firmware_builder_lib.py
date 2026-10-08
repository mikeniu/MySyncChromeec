# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions shared across firmware builder scripts."""

import argparse
import contextlib
import importlib
import json
import logging
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


_ZMAKE_DIR = str(pathlib.Path(__file__).resolve().parents[1] / "zmake")
if _ZMAKE_DIR not in sys.path:
    sys.path.insert(1, _ZMAKE_DIR)
_repository_prep = importlib.import_module("zmake.repository_prep")
CodebasePreparer = _repository_prep.CodebasePreparer
find_checkout = _repository_prep.find_checkout
find_target_repos = _repository_prep.find_target_repos
get_dirty_files = _repository_prep.get_dirty_files
set_checkout = _repository_prep.set_checkout


def _find_patch_files(patches_dir):
    """Yield all patch files recursively from patches_dir."""
    for root, dirs, files in os.walk(patches_dir):
        if ".git" in dirs:
            dirs.remove(".git")
        for file in files:
            if file.endswith(".patch"):
                yield pathlib.Path(root) / file


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
    real_checkout,
    shadow_root,
    modules,
    zephyr_base,
    src_override_dir=None,
    patches_dir=None,
):
    """Create a shadow checkout with copies of target repos and symlinks."""
    real_root = pathlib.Path(real_checkout).resolve()
    shadow_path = pathlib.Path(shadow_root).resolve()
    (shadow_path / ".repo").mkdir(parents=True, exist_ok=True)
    override_root = (
        pathlib.Path(src_override_dir).resolve() if src_override_dir else None
    )
    patch_root = pathlib.Path(patches_dir).resolve() if patches_dir else None

    paths_to_handle = {
        "zephyr_base": zephyr_base,
        "ec_private": real_root / "src" / "platform" / "ec-private",
        "chromiumos_overlay": (
            real_root / "src" / "third_party" / "chromiumos-overlay"
        ),
    }

    for name, path in modules.items():
        paths_to_handle[f"module:{name}"] = path

    copied_rel_paths = set()
    for key, path in paths_to_handle.items():
        if not path.exists():
            continue
        try:
            rel_path = path.resolve().relative_to(real_root)
        except ValueError:
            continue
        dst_path = shadow_path / rel_path

        need_copy = key == "module:ec"
        if not need_copy and override_root and override_root.exists():
            override_sub = override_root / rel_path
            if override_sub.exists() and any(
                _find_override_files(override_sub)
            ):
                need_copy = True
        if not need_copy and patch_root and patch_root.exists():
            patch_sub = patch_root / rel_path
            if patch_sub.exists() and any(_find_patch_files(patch_sub)):
                need_copy = True

        if need_copy:
            copied_rel_paths.add(rel_path)
            print(f"Copying {rel_path} to shadow checkout...")
            dst_path.parent.mkdir(parents=True, exist_ok=True)
            shutil_ignore = shutil.ignore_patterns(
                ".git", "build", "twister-out", "twister-out.*"
            )
            shutil.copytree(path, dst_path, ignore=shutil_ignore, symlinks=True)
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

    zp_rel = pathlib.Path("src/third_party/zephyrproject")
    zp_touched = any(
        rel == zp_rel or zp_rel in rel.parents for rel in copied_rel_paths
    )
    real_zephyrproject_git = real_root / zp_rel / ".git"
    shadow_zephyrproject = shadow_path / zp_rel
    if (
        not zp_touched
        and real_zephyrproject_git.exists()
        and shadow_zephyrproject.is_dir()
        and not (shadow_zephyrproject / ".git").exists()
    ):
        os.symlink(
            real_zephyrproject_git.resolve(), shadow_zephyrproject / ".git"
        )


@contextlib.contextmanager
def prepared_workspace(opts, modules):
    """Context manager to prepare a shadow or in-place workspace."""
    real_checkout = find_checkout()
    logging.basicConfig(level=logging.INFO)

    src_override_dir = getattr(opts, "src_override_dir", None)
    patches_dir = getattr(opts, "patches_dir", None)
    has_overrides = bool(
        src_override_dir
        and pathlib.Path(src_override_dir).exists()
        and any(_find_override_files(pathlib.Path(src_override_dir)))
    )
    has_patches = bool(
        patches_dir
        and pathlib.Path(patches_dir).exists()
        and any(_find_patch_files(pathlib.Path(patches_dir)))
    )

    prev_prepared = os.environ.get("ZMAKE_WORKSPACE_PREPARED")

    def _restore_env():
        if prev_prepared is None:
            os.environ.pop("ZMAKE_WORKSPACE_PREPARED", None)
        else:
            os.environ["ZMAKE_WORKSPACE_PREPARED"] = prev_prepared

    if has_overrides or has_patches:
        print("Using shadow workspace for build...")
        target_repos = find_target_repos(
            real_checkout, src_override_dir, patches_dir
        )
        user_dirty_files = set()
        for repo in target_repos:
            user_dirty_files.update(get_dirty_files(repo, real_checkout))

        ec_rel_path = modules["ec"].resolve().relative_to(real_checkout)
        zephyr_base = (
            real_checkout / "src" / "third_party" / "zephyrproject" / "zephyr"
        )

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
                    src_override_dir,
                    patches_dir,
                )
                with CodebasePreparer(
                    shadow_checkout,
                    patches_dir,
                    src_override_dir,
                    user_dirty_files,
                ):
                    os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                    shadow_ready = True
                    yield shadow_checkout / ec_rel_path
            finally:
                if shadow_ready:
                    copy_outputs_back(
                        shadow_checkout, real_checkout, ec_rel_path
                    )
                set_checkout(prev_checkout)
                _restore_env()
    else:
        try:
            with CodebasePreparer(real_checkout, patches_dir, src_override_dir):
                os.environ["ZMAKE_WORKSPACE_PREPARED"] = "1"
                yield None
        finally:
            _restore_env()


def get_safe_lcov_workers(requested_cpus):
    """Dynamically calculate the number of safe lcov/genhtml workers to avoid OOM.

    Queries /proc/meminfo and bounds to 60% of available memory, assuming 2GB max per worker.
    """
    try:
        requested = max(1, int(requested_cpus)) if requested_cpus else 4
    except (TypeError, ValueError):
        requested = 4

    try:
        with open("/proc/meminfo", "r", encoding="utf-8") as f:
            meminfo = f.read()

        available_kb = None
        for line in meminfo.splitlines():
            if line.startswith("MemAvailable:"):
                available_kb = int(line.split()[1])
                break

        if available_kb is None:
            # Fallback to MemFree if MemAvailable is missing
            for line in meminfo.splitlines():
                if line.startswith("MemFree:"):
                    available_kb = int(line.split()[1])
                    break

        if available_kb is not None:
            # Target CPU count based on memory pool
            available_gb = available_kb / (1024 * 1024)
            allowed_by_mem = int((available_gb * 0.6) / 2.0)
            safe_workers = max(1, min(requested, allowed_by_mem))

            print(
                f"INFO: Parallel test bound - Requested CPUs: {requested}, "
                f"Available system memory: {available_gb:.1f}GB, "
                f"Allowed by memory (60% at 2GB/worker): {allowed_by_mem}, "
                f"Decided workers: {safe_workers}"
            )
            return safe_workers

    except (OSError, ValueError, IndexError) as e:
        print(f"WARNING: Failed to dynamically read system memory - {e}")

    safe_workers = max(1, min(requested, 8))
    print(
        f"INFO: Parallel test bound (fallback) - Requested CPUs: {requested}, "
        f"Decided workers: {safe_workers}"
    )
    return safe_workers


def save_or_load_shard_info(opts, zephyr_dir=None, save=False):
    """Persist or load shard_count and shard_index across build/test/bundle."""
    if zephyr_dir is None:
        zephyr_dir = pathlib.Path(__file__).resolve().parent.parent
    shard_file = zephyr_dir.parent / "build" / "zephyr" / ".shard_info.json"
    if opts.shard_count is not None or opts.shard_index is not None:
        if (
            save
            and opts.shard_count
            and opts.shard_index
            and 1 <= opts.shard_index <= opts.shard_count
        ):
            shard_file.parent.mkdir(parents=True, exist_ok=True)
            with open(shard_file, "w", encoding="utf-8") as file:
                json.dump(
                    {
                        "shard_count": opts.shard_count,
                        "shard_index": opts.shard_index,
                    },
                    file,
                )
    elif shard_file.is_file():
        try:
            with open(shard_file, "r", encoding="utf-8") as file:
                data = json.load(file)
            opts.shard_count = data.get("shard_count")
            opts.shard_index = data.get("shard_index")
        except (OSError, ValueError):
            pass


def shard_projects(projects, opts=None):
    """Sort projects and return the subset for the configured shard."""
    projects.sort(key=lambda x: x.config.project_name)
    if opts and (opts.shard_count is not None or opts.shard_index is not None):
        if (
            not opts.shard_count
            or not opts.shard_index
            or opts.shard_count < 1
            or opts.shard_index < 1
            or opts.shard_index > opts.shard_count
        ):
            raise ValueError(
                f"Invalid shard_index {opts.shard_index} for shard_count {opts.shard_count}"
            )
        total = len(projects)
        chunk_size = total // opts.shard_count
        remainder = total % opts.shard_count
        start = (opts.shard_index - 1) * chunk_size + min(
            opts.shard_index - 1, remainder
        )
        end = start + chunk_size + (1 if opts.shard_index <= remainder else 0)
        return projects[start:end]
    return projects


def merge_shard_coverage_reports(
    opts,
    zephyr_dir,
    log_cmd_fn,
    get_bundle_dir_fn,
    write_metadata_fn,
    firmware_pb2_mod,
):
    """Extract shard coverage archives, merge lcov.info, and generate HTML/tbz2."""
    merge_dir = pathlib.Path(opts.merge_dir)
    if not merge_dir.is_dir() and (zephyr_dir / opts.merge_dir).is_dir():
        merge_dir = zephyr_dir / opts.merge_dir
    if not merge_dir.is_dir():
        print(f"Error: Merge directory {opts.merge_dir} does not exist.")
        return 1

    platform_ec = zephyr_dir.parent
    out_dir = platform_ec / "build" / "zephyr"
    tarball_name = "coverage.tbz2"
    tarball_path = out_dir / tarball_name
    merged_info = out_dir / "lcov.info"

    tbz_files = [
        p
        for p in sorted(merge_dir.rglob("*.tbz2"))
        if p.resolve() != tarball_path.resolve()
    ]
    extracted_infos = []

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = pathlib.Path(tmpdir)
        for idx, tbz_path in enumerate(tbz_files):
            shard_tmp = tmp_path / f"shard_{idx}_{tbz_path.stem}"
            shard_tmp.mkdir(parents=True, exist_ok=True)
            cmd = ["tar", "xvjf", str(tbz_path.resolve())]
            log_cmd_fn(cmd, cwd=shard_tmp)
            subprocess.run(
                cmd, cwd=shard_tmp, check=True, stdin=subprocess.DEVNULL
            )
            extracted_infos.extend(sorted(shard_tmp.rglob("*.info")))

        # Also support any raw .info files placed in merge_dir
        extracted_infos.extend(
            p
            for p in sorted(merge_dir.rglob("*.info"))
            if p.resolve() != merged_info.resolve()
        )

        if not extracted_infos:
            print(f"Error: No .info files found to merge in {merge_dir}")
            return 1

        out_dir.mkdir(parents=True, exist_ok=True)
        max_lcov_workers = get_safe_lcov_workers(opts.cpus)
        # Note: LCOV 2.0+ requires repeating error tokens (e.g. "gcov,gcov")
        # to suppress both the initial error and subsequent occurrences,
        # matching zephyr/Makefile.cq.
        lcov_cmd = [
            "/usr/bin/lcov",
            "--parallel",
            str(max_lcov_workers),
            "--rc",
            "branch_coverage=1",
            "--rc",
            "no_exception_branch=1",
            "--ignore-errors",
            "inconsistent,inconsistent",
            "--ignore-errors",
            "unused,unused",
            "--ignore-errors",
            "gcov,gcov",
            "-o",
            str(merged_info),
        ]
        for info_file in extracted_infos:
            lcov_cmd.extend(["-a", str(info_file)])

        log_cmd_fn(lcov_cmd)
        subprocess.run(
            lcov_cmd, cwd=zephyr_dir, check=True, stdin=subprocess.DEVNULL
        )

        html_dir = out_dir / "html"
        if html_dir.exists():
            shutil.rmtree(html_dir)
        html_dir.mkdir(parents=True, exist_ok=True)
        genhtml_cmd = [
            "/usr/bin/genhtml",
            "--parallel",
            str(max_lcov_workers),
            "--branch-coverage",
            "-q",
            "-o",
            str(html_dir),
            "--ignore-errors",
            "inconsistent,inconsistent",
            "--ignore-errors",
            "unused,unused",
            "-t",
            "All boards and tests merged",
            "-s",
            str(merged_info),
        ]
        log_cmd_fn(genhtml_cmd)
        subprocess.run(
            genhtml_cmd, cwd=zephyr_dir, check=True, stdin=subprocess.DEVNULL
        )

        # The parent recipe orchestrator (recipes/build_firmware.py) uploads
        # build/zephyr/coverage.tbz2 directly to GCS after merge-shards, so
        # both lcov.info and html/ are intentionally archived together here.
        tar_cmd = ["tar", "cvjf", str(tarball_path), "lcov.info", "html/"]
        log_cmd_fn(tar_cmd, cwd=out_dir)
        subprocess.run(
            tar_cmd, cwd=out_dir, check=True, stdin=subprocess.DEVNULL
        )

        bundle_dir = get_bundle_dir_fn(opts)
        if bundle_dir.resolve() != out_dir.resolve():
            shutil.copyfile(tarball_path, bundle_dir / tarball_name)
            bundle_html_dir = bundle_dir / "html"
            if bundle_html_dir.exists():
                shutil.rmtree(bundle_html_dir)
            shutil.copytree(html_dir, bundle_html_dir)

        info = (
            firmware_pb2_mod.FirmwareArtifactInfo()  # pylint: disable=no-member
        )
        info.bcs_version_info.version_string = opts.bcs_version
        meta = info.objects.add()
        meta.file_name = tarball_name
        meta.lcov_info.type = (
            firmware_pb2_mod.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV  # pylint: disable=no-member
        )
        meta = info.objects.add()
        meta.file_name = "html"
        meta.coverage_html.SetInParent()
        write_metadata_fn(opts, info)

    return 0


def _parse_pos_int(val):
    """Parse a positive integer string, or return None."""
    return int(val) if val and str(val).isdigit() and int(val) > 0 else None


def create_arg_parser(build, bundle, test, merge=None):
    """Parse all command line args and return opts dict."""
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--cpus",
        type=int,
        default=multiprocessing.cpu_count(),
        help="The number of cores to use.",
    )

    parser.add_argument(
        "--metrics",
        dest="metrics",
        required=False,
        default="/tmp/metrics.json",
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
        "--html",
        action="store_true",
        default=False,
        help="Generate HTML coverage reports during bundle.",
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

    parser.add_argument(
        "--firmware-targets",
        required=False,
        default="",
        help="Comma-separated list of build/test target shards",
    )

    shard_count_default = _parse_pos_int(os.environ.get("FIRMWARE_SHARD_COUNT"))
    shard_index_default = _parse_pos_int(os.environ.get("FIRMWARE_SHARD_INDEX"))
    for flag in os.environ.get("USE", "").split():
        for prefix in ("firmware_shard_count_", "shard_count_"):
            if flag.startswith(prefix):
                parsed = _parse_pos_int(flag[len(prefix) :])
                if parsed is not None:
                    shard_count_default = parsed
                break
        for prefix in ("firmware_shard_index_", "shard_index_"):
            if flag.startswith(prefix):
                parsed = _parse_pos_int(flag[len(prefix) :])
                if parsed is not None:
                    shard_index_default = parsed
                break

    parser.add_argument(
        "--shard-count",
        type=int,
        required=False,
        default=shard_count_default,
        help="Total number of build shards to divide projects/tests into.",
    )

    parser.add_argument(
        "--shard-index",
        type=int,
        required=False,
        default=shard_index_default,
        help="1-based index of the current shard to execute.",
    )

    parser.add_argument(
        "--toolchain",
        choices=("all", "host/gnu", "host/llvm"),
        default="all",
        help="Toolchain to use for running unit tests.",
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
    test_cmd.add_argument(
        "--toolchain",
        choices=("all", "host/gnu", "host/llvm"),
        default=argparse.SUPPRESS,
        help="Toolchain to use for running unit tests.",
    )
    test_cmd.set_defaults(func=test)

    if merge:
        merge_cmd = sub_cmds.add_parser(
            "merge-shards",
            help="Merges coverage artifacts from multiple build shards",
        )
        merge_cmd.add_argument(
            "--merge-dir",
            required=True,
            help="Directory containing shard coverage.tbz2 files to merge.",
        )
        merge_cmd.set_defaults(func=merge)

    return parser, sub_cmds
