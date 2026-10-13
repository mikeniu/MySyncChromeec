# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Helper functions shared across firmware builder scripts."""

import argparse
import collections
import json
import multiprocessing
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


_ZMAKE_DIR = str(pathlib.Path(__file__).resolve().parents[1] / "zmake")
if _ZMAKE_DIR not in sys.path:
    sys.path.insert(1, _ZMAKE_DIR)


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
    projects.sort(
        key=lambda x: x.config.project_name if hasattr(x, "config") else str(x)
    )
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

        # Only generate HTML reports if explicitly requested (--html) or during
        # postsubmit release builds (--bcs-version), matching bundle_coverage().
        # Presubmit CQ runs without --bcs-version and only needs lcov.info
        # inside coverage.tbz2 for Gerrit/Zoss coverage display.
        generate_html = bool(opts.html or opts.bcs_version)
        html_dir = out_dir / "html"
        if html_dir.exists():
            shutil.rmtree(html_dir)
        if generate_html:
            html_dir.mkdir(parents=True, exist_ok=True)
            # Match zephyr/Makefile.cq: run genhtml without --parallel to avoid OOM
            # when rendering thousands of source files in Perl child processes.
            genhtml_cmd = [
                "/usr/bin/genhtml",
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
                genhtml_cmd,
                cwd=zephyr_dir,
                check=True,
                stdin=subprocess.DEVNULL,
            )

        # The parent recipe orchestrator (recipes/build_firmware.py) uploads
        # build/zephyr/coverage.tbz2 directly to GCS after merge-shards.
        tar_cmd = ["tar", "cvjf", str(tarball_path), "lcov.info"]
        if generate_html:
            tar_cmd.append("html/")
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
            if generate_html:
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
        if generate_html:
            meta = info.objects.add()
            meta.file_name = "html"
            meta.coverage_html.SetInParent()
        write_metadata_fn(opts, info)

    return 0


def summarize_merged_lcov(lcov_info, boards, test_exclude, board_exclude):
    """Computes the code coverage metrics of a merged lcov.info.

    These are the ALL_FILTERED, ALL_MERGED and BOARD_* metrics that test() gets
    by running lcov --summary on the outputs of Makefile.cq, but computed in a
    single pass over the merged tracefile, instead of running lcov and
    lcov_stencil.py on it for each metric. ALL_TESTS, EC_ZEPHYR_TESTS and
    EC_ZEPHYR_MERGED are not computed, because they include the files that the
    shards filter out of their lcov.info.

    The hits of the unit tests are under the empty test name (TN:), and the
    lines that each board compiles, without hits, are under TN:<board name>.

    Args:
        lcov_info: Path of the merged lcov.info.
        boards: Names of the boards to compute the BOARD_* metrics of.
        test_exclude: Compiled regex of the paths that ALL_FILTERED skips, or
            None.
        board_exclude: Compiled regex of the paths that BOARD_* skip, or None.

    Returns:
        A dict of metric name to (covered lines, total lines).

    Raises:
        ValueError: If a metric has no lines.
    """
    # geninfo replaces the characters that are not allowed in a test name.
    board_names = {re.sub(r"\W", "_", board): board for board in boards}
    all_lines = collections.defaultdict(set)
    hit_lines = collections.defaultdict(set)
    board_lines = {name: collections.defaultdict(set) for name in board_names}
    test_name = source = None
    with open(lcov_info, encoding="utf-8") as file:
        for line in file:
            if line.startswith("TN:"):
                test_name = line[3:].rstrip("\n")
            elif line.startswith("SF:"):
                source = line[3:].rstrip("\n")
            elif line.startswith("DA:"):
                number, count = line[3:].split(",")[:2]
                all_lines[source].add(number)
                if int(count) > 0:
                    hit_lines[source].add(number)
                if test_name in board_lines:
                    board_lines[test_name][source].add(number)
            elif line.startswith("end_of_record"):
                test_name = source = None

    def summarize(lines_by_source, exclude):
        covered = total = 0
        for path, lines in lines_by_source.items():
            if exclude and exclude.match(path):
                continue
            total += len(lines)
            covered += len(lines & hit_lines[path])
        return covered, total

    metrics = {
        "ALL_FILTERED": summarize(all_lines, test_exclude),
        "ALL_MERGED": summarize(all_lines, None),
    }
    for name, board in board_names.items():
        metrics[f"BOARD_{board}".upper()] = summarize(
            board_lines[name], board_exclude
        )
    for name, (_, total) in metrics.items():
        if not total:
            raise ValueError(f"{lcov_info} has no lines for {name}")
    return metrics


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
        action="append",
        dest="patches_dirs",
        metavar="PATCHES_DIR",
        help="Path to a patches directory. Can be repeated.",
    )

    parser.add_argument(
        "--src-override-dir",
        action="append",
        dest="src_override_dirs",
        metavar="SRC_OVERRIDE_DIR",
        help="Path to a source override directory. Can be repeated.",
    )

    parser.add_argument(
        "--no-shadow",
        action="store_true",
        default=False,
        help="Do not use a shadow workspace; apply changes in-place.",
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
