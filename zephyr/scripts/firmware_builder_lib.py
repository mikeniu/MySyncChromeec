# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions shared across firmware builder scripts."""

import argparse
import json
import multiprocessing
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


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
