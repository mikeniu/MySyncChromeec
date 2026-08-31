#!/usr/bin/env python3
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build, bundle, or test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import argparse
import hashlib
import multiprocessing
import os
import re
import shutil
import subprocess
import sys

# pylint: disable=import-error
from google.protobuf import json_format


# Find chromite!  Assume this code only runs inside the SDK.
sys.path.insert(0, "/mnt/host/source")

# pylint: disable=wrong-import-position
from chromite.api.gen_sdk.chromite.api import firmware_pb2


DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(DIR, "build")

# Set to True to publish goldeneye artifacts with new firmware builders.
#   - False on TOT.
#   - True on mp and prepvt branches.
PUBLISH_TO_GOLDENEYE = False

# Cr50 uses the reef builder. If that ever changes, update this name
GE_BOARD = "reef"
DEFAULT_BUNDLE_DIRECTORY = "/tmp/artifact_bundles"
DEFAULT_BUNDLE_METADATA_FILE = "/tmp/artifact_bundle_metadata"
RO_VER = "0.0.14"
# List of files to bundle each element is a tuple with the source and dest
# filenames. If the dest filename is empty, it'll keep the same basename.
# This is the same list of files the ebuild bundles.
BUNDLE_FILES = [
    ("ec.bin", ""),
    ("RW/updated.manifest.json", "prod.json"),
    ("RW/ec.RW_B.elf.fips", "ec.RW_B.elf"),
    ("RW/ec.RW_B.map", ""),
    ("RW/ec.RW.dis", ""),
    ("RW/ec.RW.elf.fips", "ec.RW.elf"),
    ("RW/ec.RW.map", ""),
    ("RW/ec.RW.flat.hashes.hashes", "ec.RW.hashes"),
    ("RW/ec.RW_B.flat.hashes.hashes", "ec.RW_B.hashes"),
    ("RW/space_free_ram.txt", ""),
    ("RW/space_free_flash.txt", ""),
    ("../../util/signer/fuses.xml", ""),
    ("../../board/cr50/rma_key_blob.x25519.prod", ""),
    ("../../board/cr50/rma_key_blob.x25519.test", ""),
    ("../../board/cr50/rma_key_blob.p256.prod", ""),
    ("../../board/cr50/rma_key_blob.p256.test", ""),
    (
        "../../board/cr50/ROs/cr50.prod.ro.%s.%s.hex" % ("A", RO_VER),
        "prod.ro.A",
    ),
    (
        "../../board/cr50/ROs/cr50.prod.ro.%s.%s.hex" % ("B", RO_VER),
        "prod.ro.B",
    ),
]


def run_cmd(cmd, exec_dir=None):
    """Run the passed in command, optionally in a different directory.

    By default run the commands in this script's directory.

    Return command's console output.
    """
    if exec_dir is None:
        exec_dir = DIR
    else:
        exec_dir = os.path.realpath(exec_dir)
    print(f"# Running \"{' '.join(cmd)}\" in {exec_dir}")
    result = subprocess.run(
        cmd,
        cwd=exec_dir,
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout.decode().strip()


def log_step_title(name):
    """Clearly log a step's title"""
    title = f"##### START {name} #####"
    hashes = "#" * len(title)
    print("\n%s\n%s\n%s\n" % (hashes, title, hashes), flush=True)


def get_repo_branch(repo_path):
    """Determine a user-friendly branch name for a repository.

    Resolves local branch, tracking branch, manifest branch (m/main), or
    remote branch pointing at HEAD, avoiding generic 'HEAD' outputs for
    detached HEADs.
    """
    res = subprocess.run(
        ["git", "-C", repo_path, "symbolic-ref", "-q", "--short", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    local_branch = res.stdout.strip()

    upstream = subprocess.run(
        ["git", "-C", repo_path, "rev-parse", "--abbrev-ref", "@{upstream}"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()

    m_main = ""
    m_res = subprocess.run(
        ["git", "-C", repo_path, "symbolic-ref", "-q", "refs/remotes/m/main"],
        capture_output=True,
        text=True,
        check=False,
    )
    if m_res.returncode == 0:
        m_ref = m_res.stdout.strip()
        if m_ref.startswith("refs/remotes/"):
            m_main = m_ref[len("refs/remotes/") :]

    if local_branch:
        if upstream:
            return f"{local_branch} (tracking {upstream})"
        elif m_main:
            return f"{local_branch} (manifest: {m_main})"
        return local_branch

    # Detached HEAD state: resolve remote branch
    points_at = (
        subprocess.run(
            [
                "git",
                "-C",
                repo_path,
                "for-each-ref",
                "--format=%(refname:short)",
                "--points-at",
                "HEAD",
                "refs/remotes/",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        .stdout.strip()
        .splitlines()
    )
    remote_refs = [
        r.strip()
        for r in points_at
        if r.strip() and not r.strip().startswith("m/")
    ]

    if m_main:
        if remote_refs and m_main not in remote_refs:
            return f"{remote_refs[0]} (manifest: {m_main})"
        return m_main
    elif remote_refs:
        return remote_refs[0]

    desc = subprocess.run(
        ["git", "-C", repo_path, "describe", "--all", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()
    if desc:
        for prefix in ("refs/heads/", "refs/remotes/", "heads/", "remotes/"):
            if desc.startswith(prefix):
                desc = desc[len(prefix) :]
                break
        return desc

    return "HEAD"


def log_dependency_repo_info():
    """Log current branch and recent commits for key dependency repositories."""
    log_step_title("Dependency Repositories Info")
    src_dir = os.path.abspath(
        os.path.join(DIR, os.pardir, os.pardir)
    )
    repo_list_file = os.path.join(DIR, "util", "repo_dir.list")

    rel_paths = []
    if os.path.isfile(repo_list_file):
        with open(repo_list_file, "r", encoding="utf-8") as f:
            for line in f:
                rel = line.strip()
                if rel:
                    rel_paths.append(rel)

    if not rel_paths:
        branch_ext = os.environ.get(
            "BRANCH_EXT", "-private" if DIR.endswith("-private") else ""
        )
        gsc_utils_path = (
            f"platform/gsc-utils{branch_ext}"
            if os.path.exists(
                os.path.join(src_dir, f"platform/gsc-utils{branch_ext}")
            )
            else "platform/gsc-utils"
        )
        rel_paths = [
            os.path.relpath(DIR, src_dir),
            "platform/cr50-utils",
            f"platform/pinweaver{branch_ext}",
            gsc_utils_path,
            f"third_party/tpm2{branch_ext}",
            "third_party/cryptoc",
        ]

    repos = []
    for rel_path in rel_paths:
        full_path = os.path.join(src_dir, rel_path)
        if os.path.exists(full_path):
            res = subprocess.run(
                ["git", "-C", full_path, "rev-parse", "--show-toplevel"],
                capture_output=True,
                text=True,
                check=False,
            )
            if res.returncode == 0:
                git_root = res.stdout.strip()
                rel_git = os.path.relpath(git_root, src_dir)
                if rel_git not in repos:
                    repos.append(rel_git)

    for rel_git in repos:
        repo_path = os.path.join(src_dir, rel_git)
        branch = get_repo_branch(repo_path)
        commits = subprocess.run(
            ["git", "-C", repo_path, "log", "-n", "3", "--oneline"],
            capture_output=True,
            text=True,
            check=False,
        ).stdout.strip()
        print(f"=== Repository: {rel_git} (Branch: {branch}) ===")
        if commits:
            print(commits)
        else:
            print("  <no commits found>")
        print("", flush=True)


def log_compiler_info():
    """Log compiler and toolchain locations and version information."""
    log_step_title("Compiler and Toolchain Info")
    env_vars = [
        "COREBOOT_SDK_ROOT_arm",
        "CROSS_COMPILE",
        "CC",
        "CXX",
        "CFLAGS",
        "EXTRA_CFLAGS",
    ]
    print("=== Environment Variables ===")
    for var in env_vars:
        if var in os.environ:
            print(f"{var}={os.environ[var]}")
    print("", flush=True)

    sdk_root = os.environ.get("COREBOOT_SDK_ROOT_arm", "")

    target_compilers = []
    if sdk_root:
        sdk_gcc = os.path.join(sdk_root, "bin", "arm-eabi-gcc")
        if os.path.exists(sdk_gcc):
            target_compilers.append(("Coreboot SDK ARM GCC", sdk_gcc))

    which_arm_gcc = shutil.which("arm-eabi-gcc")
    if which_arm_gcc and which_arm_gcc not in [p for _, p in target_compilers]:
        target_compilers.append(("PATH arm-eabi-gcc", which_arm_gcc))

    which_arm_none_gcc = shutil.which("arm-none-eabi-gcc")
    if (
        which_arm_none_gcc
        and which_arm_none_gcc not in [p for _, p in target_compilers]
    ):
        target_compilers.append(("PATH arm-none-eabi-gcc", which_arm_none_gcc))

    if not target_compilers:
        print("No target compiler found in COREBOOT_SDK_ROOT_arm or PATH.\n")
    else:
        for desc, compiler_path in target_compilers:
            print(f"=== {desc} ===")
            print(f"Location: {compiler_path}")
            if os.path.islink(compiler_path):
                print(f"Symlink target: {os.path.realpath(compiler_path)}")
            res = subprocess.run(
                [compiler_path, "-v"],
                capture_output=True,
                text=True,
                check=False,
            )
            output = res.stderr.strip() or res.stdout.strip()
            if output:
                print("Version details (-v):")
                print(output)
            print("", flush=True)

    if sdk_root:
        sdk_ld = os.path.join(sdk_root, "bin", "arm-eabi-ld")
        if os.path.exists(sdk_ld):
            print("=== Target Linker ===")
            print(f"Location: {sdk_ld}")
            res = subprocess.run(
                [sdk_ld, "--version"],
                capture_output=True,
                text=True,
                check=False,
            )
            lines = res.stdout.strip().splitlines()
            if lines:
                print(f"Version: {lines[0]}")
            print("", flush=True)

    host_tools = [
        (
            "Host GCC",
            shutil.which("x86_64-pc-linux-gnu-gcc") or shutil.which("gcc"),
        ),
        ("Host Clang", shutil.which("clang")),
        ("Ccache", shutil.which("ccache")),
    ]
    for name, tool_path in host_tools:
        if tool_path:
            print(f"=== {name} ===")
            print(f"Location: {tool_path}")
            res = subprocess.run(
                [tool_path, "--version"],
                capture_output=True,
                text=True,
                check=False,
            )
            lines = res.stdout.strip().splitlines()
            if lines:
                print(f"Version: {lines[0]}")
            print("", flush=True)


def init_toolchain():
    """Initialize coreboot-sdk.

    Returns:
        Environment variables to use for toolchain.
    """
    # (environment variable, bazel target)
    toolchains = [
        ("COREBOOT_SDK_ROOT_arm", "@cr50-coreboot-sdk-arm-eabi//:get_path"),
    ]

    run_cmd(
        [
            "bazel",
            "--project",
            "fwsdk",
            "build",
            *(target for _, target in toolchains),
        ]
    )

    for name, target in toolchains:
        run_result = run_cmd(["bazel", "--project", "fwsdk", "run", target])
        os.environ[name] = run_result

    log_compiler_info()


def build_codesigner():
    """Build codesigner utility if it is not there"""
    codesigner_root = os.path.realpath(
        os.path.join(DIR, "../cr50-utils/software/tools/codesigner")
    )
    run_cmd(["make", "codesigner"], exec_dir=codesigner_root)


def build(opts):
    """Builds all Cr50 firmware targets"""
    metrics = firmware_pb2.FwBuildMetricList()
    init_toolchain()

    if opts.code_coverage:
        print(
            "When --code-coverage is selected, 'build' is a no-op. "
            "Run 'test' with --code-coverage instead."
        )
        return

    # Codesigner is needed for images' hashes generation.
    build_codesigner()

    base_cmd = ["make", "BOARD=cr50", "all", "dis", f"-j{opts.cpus}"]

    run_cmd(base_cmd + ["PROD_BUILD_MODE=1"])
    add_size_metrics(metrics, "ro-prod", f"{BUILD_DIR}/cr50/RO/ec.RO.map")
    add_size_metrics(metrics, "rw-prod", f"{BUILD_DIR}/cr50/RW/ec.RW.map")
    run_cmd(base_cmd + ["out=build/dbg_test", "CR50_DEV=1"])
    add_size_metrics(metrics, "ro-dev", f"{BUILD_DIR}/dbg_test/RO/ec.RO.map")
    add_size_metrics(metrics, "rw-dev", f"{BUILD_DIR}/dbg_test/RW/ec.RW.map")
    run_cmd(base_cmd + ["out=build/crypto_test", "CRYPTO_TEST=1"])
    run_cmd(
        base_cmd
        + ["out=build/crypto_test_rb", "CRYPTO_TEST=1", "H1_RED_BOARD=1"]
    )

    # Build MP Cr50 image
    run_cmd(
        base_cmd
        + [
            "out=build/mp_build",
            "PROD_BUILD_MODE=1",
            "BRANCH=MP",
        ]
    )

    # Build PREPVT Cr50 image
    run_cmd(
        base_cmd
        + [
            "out=build/prepvt_build",
            "PROD_BUILD_MODE=1",
            "BRANCH=PREPVT",
        ]
    )

    with open(opts.metrics, "w", encoding="utf-8") as f:
        f.write(json_format.MessageToJson(metrics))


def add_size_metrics(metrics, platform_name, mapfile):
    if not os.path.exists(mapfile):
        return
    item = metrics.value.add()
    item.target_name = "cr50"
    item.platform_name = platform_name
    with open(mapfile, "r", encoding="utf-8") as mapf:
        mapfile = mapf.read()
    image_size = re.search(r"(0x[0-9a-f]+) +__image_size", mapfile)
    ram_size = re.search(r"IRAM +0x[0-9a-f]+ +(0x[0-9a-f]+) ", mapfile)
    ram_free = re.search(r"(0x[0-9a-f]+) +__ram_free", mapfile)
    if image_size:
        image_size = int(image_size.group(1), 16)
        fw_section = item.fw_section.add()
        fw_section.region = "total-flash"
        fw_section.used = image_size
        fw_section.track_on_gerrit = True
    if ram_size and ram_free:
        ram_size = int(ram_size.group(1), 16)
        ram_free = int(ram_free.group(1), 16)
        fw_section = item.fw_section.add()
        fw_section.region = "total-ram"
        fw_section.used = ram_size - ram_free
        fw_section.track_on_gerrit = True


def bundle(opts):
    if opts.code_coverage:
        bundle_coverage(opts)
    else:
        bundle_firmware(opts)


def get_bundle_dir(opts):
    """Get the directory for the bundle from opts or use the default.

    Also create the directory if it doesn't exist.
    """
    bundle_dir = (
        opts.output_dir if opts.output_dir else DEFAULT_BUNDLE_DIRECTORY
    )
    if not os.path.isdir(bundle_dir):
        os.mkdir(bundle_dir)
    return bundle_dir


def write_metadata(opts, info):
    """Write the metadata about the bundle."""
    bundle_metadata_file = (
        opts.metadata if opts.metadata else DEFAULT_BUNDLE_METADATA_FILE
    )
    with open(bundle_metadata_file, "w", encoding="utf-8") as f:
        f.write(json_format.MessageToJson(info))


def bundle_coverage(opts):
    """Bundles the artifacts from code coverage into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    tarball_name = "coverage.tbz2"
    tarball_path = os.path.join(bundle_dir, tarball_name)
    run_cmd(
        ["tar", "cvfj", tarball_path, "lcov.info"],
        exec_dir=os.path.join(DIR, "build/coverage"),
    )
    meta = info.objects.add()
    meta.file_name = tarball_name
    meta.lcov_info.type = (
        firmware_pb2.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV
    )

    write_metadata(opts, info)


def create_artifact_dir(ec_dir, build_target):
    """Copy artifacts into a build_target directory that can be bundled.

    The signer expects the artifacts to be in a build_target directory in the
    tarball. Create the directory and copy all of the artifact files into it.
    """
    # Nothing needs to be done to the host artifacts
    if build_target == "host":
        return ["--exclude=*.o.d", "--exclude=*.o", "."]

    build_dir = os.path.realpath(os.path.join(ec_dir, "build", build_target))
    run_cmd(["mkdir", build_target], exec_dir=build_dir)
    for src, dest in BUNDLE_FILES:
        dest = os.path.join(build_target, dest)
        if build_target not in ("cr50", "mp_build", "prepvt_build"):
            if dest.endswith(".hashes"):
                # Hashes are generated only for Cr50 images.
                continue
        # The non-cr50 builds are DBG and crypto test images. Rename their elf
        # files, so it's not possible for the signer to sign them.
        if dest.endswith(".elf") and build_target != "cr50":
            dest += ".test"
        run_cmd(["cp", src, dest], exec_dir=build_dir)
        # Calculate a sha256 digest of the combined set of hashes generated by
        # codesigner.
        if dest.endswith(".hashes"):
            dest = dest.replace(".hashes", ".digest")
            with open(os.path.join(build_dir, src), "rb") as hashf:
                digest = hashlib.sha256(hashf.read()).digest()
                with open(os.path.join(build_dir, dest), "wb") as f:
                    f.write(digest)
    return [build_target]


def bundle_firmware(opts):
    """Bundles the artifacts from each target into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    for build_target in sorted(os.listdir(os.path.join(ec_dir, "build"))):
        tarball_name = "".join([build_target, ".firmware.tar.bz2"])
        tarball_path = os.path.join(bundle_dir, tarball_name)

        artifact_dir = create_artifact_dir(ec_dir, build_target)
        run_cmd(
            ["tar", "cvfj", tarball_path] + artifact_dir,
            exec_dir=os.path.join(DIR, "build", build_target),
        )
        meta = info.objects.add()
        meta.file_name = tarball_name
        # Board is required to publish to GE as well
        if PUBLISH_TO_GOLDENEYE and build_target == "cr50":
            meta.tarball_info.board.extend([GE_BOARD])
            meta.tarball_info.publish_to_goldeneye = True
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.GSC
        )
        # TODO(kmshelton): Populate the rest of metadata contents as it gets
        # defined in infra/proto/src/chromite/api/firmware.proto.

    write_metadata(opts, info)


def test(opts):
    """Runs all of the unit tests for EC firmware"""
    metrics = firmware_pb2.FwTestMetricList()
    init_toolchain()
    with open(opts.metrics, "w", encoding="utf-8") as f:
        f.write(json_format.MessageToJson(metrics))

    # If building for code coverage, build the 'coverage' target, which
    # builds the posix-based unit tests for code coverage and assembles
    # the LCOV information.
    #
    # Otherwise, build the 'runtests' target, which verifies all
    # posix-based unit tests build and pass.
    target = "coverage" if opts.code_coverage else "runtests"
    run_cmd(["make", target, f"-j{opts.cpus}"])


def main(args):
    """Builds, bundles, or tests all of the EC targets.

    Additionally, the tool reports build metrics.
    """
    opts = parse_args(args)

    if not hasattr(opts, "func"):
        print("Must select a valid sub command!")
        return -1

    log_dependency_repo_info()

    # Run selected sub command function
    try:
        opts.func(opts)
    except subprocess.CalledProcessError:
        return 1
    else:
        return 0


def parse_args(args):
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--cpus",
        default=multiprocessing.cpu_count(),
        help="The number of cores to use.",
    )

    parser.add_argument(
        "--metrics",
        dest="metrics",
        required=True,
        help="File to write the json-encoded MetricsList proto message.",
    )

    parser.add_argument(
        "--metadata",
        required=False,
        help="Full pathname for the file in which to write build artifact "
        "metadata.",
    )

    parser.add_argument(
        "--output-dir",
        required=False,
        help="Full pathanme for the directory in which to bundle build "
        "artifacts.",
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

    # Would make this required=True, but not available until 3.7
    sub_cmds = parser.add_subparsers()

    build_cmd = sub_cmds.add_parser("build", help="Builds all firmware targets")
    build_cmd.set_defaults(func=build)

    build_cmd = sub_cmds.add_parser(
        "bundle",
        help="Creates a tarball containing build "
        "artifacts from all firmware targets",
    )
    build_cmd.set_defaults(func=bundle)

    test_cmd = sub_cmds.add_parser("test", help="Runs all firmware unit tests")
    test_cmd.set_defaults(func=test)

    return parser.parse_args(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
