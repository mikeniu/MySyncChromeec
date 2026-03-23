#!/usr/bin/env -S python3 -u
# -*- coding: utf-8 -*-
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Build, bundle, or test all of the EC boards.

This is the entry point for the custom firmware builder workflow recipe.  It
gets invoked by chromite/api/controller/firmware.py.
"""

import json
import os
import pathlib
import shutil
import subprocess
import sys

# pylint: disable=import-error
from google.protobuf import json_format
from util.coreboot_sdk import init_toolchain

# cros format puts this here
import yaml  # pylint: disable=wrong-import-order

# pylint: disable=wrong-import-order
from chromite.api.gen_sdk.chromite.api import firmware_pb2
from chromite.lib.chromeos_version import VersionInfo
import scripts.firmware_builder_lib


DEFAULT_BUNDLE_DIRECTORY = "/tmp/artifact_bundles"
DEFAULT_BUNDLE_METADATA_FILE = "/tmp/artifact_bundle_metadata"

# The the list of boards whose on-device unit tests we will verify compilation.
# TODO(b/172501728) On-device unit tests should build for all boards, but
# they've bit rotted, so we only build the ones that compile.
BOARDS_UNIT_TEST = [
    "bloonchipper",
    "dartmonkey",
    "helipilot",
]

# Interesting regions to show in gerrit
BINARY_SIZE_REGIONS = ["RW_FLASH", "RW_IRAM"]

# The most recently edited boards that should show binary size changes in
# gerrit
BINARY_SIZE_BOARDS = [
    "dexi",
    "dibbi",
    "dita",
    "gaelin",
    "gladios",
    "lisbon",
    "marasov",
    "moli",
    "prism",
    "shotzo",
    "taranza",
]

# These EC names in boxter don't exist. They come from somewhere else
EXPECTED_MISSING_BOARDS = {
    # Wilco devices don't have Chrome ECs
    "arcada_signed",  # Remove 2029-07
    "arcada",  # Remove 2029-07
    "drallion_signed",  # Remove 2030-06
    "drallion",  # Remove 2030-06
    "sarien_signed",  # Remove 2029-07
    "sarien",  # Remove 2029-07
    # Hana seems to have been deleted from HEAD, see firmware-oak-8438.B branch
    "hana",  # Remove 2027-06
    # These are zephyr, not ECOS
    "brox-ish",
    "rex-ish",
}

# These EC boards are not used in boxter for various reasons.
EXPECTED_UNUSED_BOARDS = {
    # Ti50 test board carrier.
    "hyperdebug",
    # Servo. These could probably be deleted, and built only from the servo branch
    "c2d2",
    "servo_micro",
    "servo_v4",
    "servo_v4p1",
    # Other hardware tools, no idea when these are AUE
    "baklava",
    "panqueque",
    "quiche",
    # Detachable keyboards, see src/platform2/hammerd/hammertests/common.py
    "duck",  # Remove 2031-06
    "eel",  # Remove 2031-06
    "hammer",  # Not used directly, but all the other keyboards use this code.
    "staff",  # Remove 2027-08
    "star",  # Remove 2031-06
    "whiskers",  # Remove 2029-06
    "zed",  # Remove 2031-06
    # Fingerprint development
    "bloonchipper-druid",
    "hatch_fp",
    "nocturne_fp",
    "nucleo-dartmonkey",
    "nucleo-f412zg",
    "nucleo-h743zi",
    "rosalia",
    # Symlinks
    "icarus",  # cozmo links to this 2030-06
    "poppy",  # soraka links to this 2027-08
    # SCP. These have no builders, the binaries are copied to GCS manually.
    # src/platform/dev/contrib/uprev_scp_firmware.sh
    "asurada_scp",  # chromeos-base/chromeos-scp-firmware-asurada
    "corsola_scp",  # chromeos-base/chromeos-scp-firmware-corsola
    "cherry_scp",  # chromeos-base/chromeos-scp-firmware-cherry
    "cherry_scp_core1",  # chromeos-base/chromeos-scp-firmware-cherry
    "geralt_scp",  # chromeos-base/chromeos-scp-firmware-geralt
    "geralt_scp_core1",  # chromeos-base/chromeos-scp-firmware-geralt
}


def build(opts):
    """Builds all EC firmware targets

    Note that when we are building unit tests for code coverage, we don't
    need this step. It builds EC **firmware** targets, but unit tests with
    code coverage are all host-based. So if the --code-coverage flag is set,
    we don't need to build the firmware targets and we can return without
    doing anything but creating the metrics file and giving an informational
    message.
    """
    metric_list = firmware_pb2.FwBuildMetricList()  # pylint: disable=no-member
    env = os.environ.copy()
    env.update(init_toolchain())

    if opts.code_coverage:
        print(
            "When --code-coverage is selected, 'build' is a no-op. "
            "Run 'test' with --code-coverage instead."
        )
        with open(opts.metrics, "w", encoding="utf-8") as file:
            file.write(json_format.MessageToJson(metric_list))
        return

    cmd = ["make", "clobber"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)

    cmd = ["make", "buildall_only", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)

    cmd = ["make", "print-all-baseboards", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    baseboards = {}
    for line in subprocess.run(
        cmd,
        cwd=os.path.dirname(__file__),
        check=True,
        universal_newlines=True,
        stdout=subprocess.PIPE,
        env=env,
    ).stdout.splitlines():
        parts = line.split("=")
        if len(parts) > 1:
            baseboards[parts[0]] = parts[1]

    ec_dir = os.path.dirname(__file__)
    build_dir = os.path.join(ec_dir, "build")
    for build_target in sorted(os.listdir(build_dir)):
        metric = metric_list.value.add()
        metric.target_name = build_target
        metric.platform_name = build_target
        if build_target in baseboards and baseboards[build_target]:
            metric.platform_name = baseboards[build_target]

        for variant in ["RO", "RW"]:
            memsize_file = (
                pathlib.Path(build_dir)
                / build_target
                / variant
                / f"ec.{variant}.elf.memsize.txt"
            )
            if memsize_file.exists():
                parse_memsize(
                    memsize_file,
                    metric,
                    variant,
                    build_target in BINARY_SIZE_BOARDS,
                )
    with open(opts.metrics, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(metric_list))

    gcc_build_dir = build_dir + ".gcc"
    try:
        # b/352025405: build_with_clang.py deletes the build directory, but
        # we want to preserve the gcc build artifacts for uploading (bundling).
        # Temporarily rename the gcc build output directory and restore after
        # the clang build finishes.
        os.rename(build_dir, gcc_build_dir)

        # Ensure that there are no regressions for boards that build
        # successfully with clang: b/172020503.
        cmd = ["./util/build_with_clang.py", f"-j{opts.cpus}"]
        print(f'# Running {" ".join(cmd)}.')
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)
    finally:
        shutil.rmtree(build_dir)
        os.rename(gcc_build_dir, build_dir)


UNITS = {
    "B": 1,
    "KB": 1024,
    "MB": 1024 * 1024,
    "GB": 1024 * 1024 * 1024,
}


def parse_memsize(filename, metric, variant, track_on_gerrit):
    """Parse the output of the build to extract the image size."""
    with open(filename, "r", encoding="utf-8") as infile:
        # Skip header line
        infile.readline()
        for line in infile.readlines():
            parts = line.split()
            fw_section = metric.fw_section.add()
            fw_section.region = variant + "_" + parts[0][:-1]
            fw_section.used = int(parts[1]) * UNITS[parts[2]]
            fw_section.total = int(parts[3]) * UNITS[parts[4]]
            if track_on_gerrit and fw_section.region in BINARY_SIZE_REGIONS:
                fw_section.track_on_gerrit = True
            else:
                fw_section.track_on_gerrit = False


def bundle(opts):
    """Bundle the artifacts."""
    if opts.code_coverage:
        bundle_coverage(opts)
    else:
        bundle_firmware(opts)


def get_bundle_dir(opts):
    """Get the directory for the bundle from opts or use the default.

    Also create the directory if it doesn't exist.
    """
    if opts.output_dir:
        bundle_dir = opts.output_dir
    else:
        bundle_dir = DEFAULT_BUNDLE_DIRECTORY
    if not os.path.isdir(bundle_dir):
        os.mkdir(bundle_dir)
    return bundle_dir


def write_metadata(opts, info):
    """Write the metadata about the bundle."""
    bundle_metadata_file = (
        opts.metadata if opts.metadata else DEFAULT_BUNDLE_METADATA_FILE
    )
    with open(bundle_metadata_file, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(info))


def bundle_coverage(opts):
    """Bundles the artifacts from code coverage into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
    info.bcs_version_info.version_string = opts.bcs_version
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    tarball_name = "coverage.tbz2"
    tarball_path = os.path.join(bundle_dir, tarball_name)
    cmd = ["tar", "cvfj", tarball_path, "lcov.info"]
    subprocess.run(cmd, cwd=os.path.join(ec_dir, "build/coverage"), check=True)
    meta = info.objects.add()
    meta.file_name = tarball_name
    meta.lcov_info.type = (
        firmware_pb2.FirmwareArtifactInfo.LcovTarballInfo.LcovType.LCOV  # pylint: disable=no-member
    )

    write_metadata(opts, info)


def get_version():
    """Determine the current chroot version."""
    ver = VersionInfo.from_repo(source_repo=find_checkout())
    if ver:
        return ver.VersionString()
    return None


def bundle_firmware(opts):
    """Bundles the artifacts from each target into its own tarball."""
    info = firmware_pb2.FirmwareArtifactInfo()  # pylint: disable=no-member
    info.bcs_version_info.version_string = opts.bcs_version
    version = opts.bcs_version or get_version()
    bundle_dir = get_bundle_dir(opts)
    ec_dir = os.path.dirname(__file__)
    cmd = ["make", "print-all-baseboards", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    baseboards = {}
    env = os.environ.copy()
    env.update(init_toolchain())
    for line in subprocess.run(
        cmd,
        cwd=os.path.dirname(__file__),
        check=True,
        universal_newlines=True,
        stdout=subprocess.PIPE,
        env=env,
    ).stdout.splitlines():
        parts = line.split("=")
        if len(parts) > 1:
            baseboards[parts[0]] = parts[1]

    for build_target in sorted(os.listdir(os.path.join(ec_dir, "build"))):
        if build_target in ["host"]:
            continue
        artifacts_dir = pathlib.Path(
            os.path.join(ec_dir, "build", build_target)
        )
        # karis.EC.15709.192.0.tar.bz2
        if version:
            tarball_name = f"{build_target}.EC.{version}.tar.bz2"
            elf_tarball_name = f"{build_target}.EC_elf.{version}.tar.bz2"
        else:
            tarball_name = f"{build_target}.EC.tar.bz2"
            elf_tarball_name = f"{build_target}.EC_elf.tar.bz2"

        tarball_path = os.path.join(bundle_dir, tarball_name)
        cmd = [
            "tar",
            r"--transform=s/\.config/ec.config/",
            "-cvjf",
            tarball_path,
        ]
        cmd.extend(
            [
                str(x.relative_to(artifacts_dir))
                for x in artifacts_dir.glob("*.bin")
            ]
        )
        cmd.extend(
            [
                str(x.relative_to(artifacts_dir))
                for x in artifacts_dir.glob(".config")
            ]
        )
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(
            cmd,
            cwd=artifacts_dir,
            check=True,
            stdin=subprocess.DEVNULL,
        )
        meta = info.objects.add()
        if build_target in baseboards and baseboards[build_target]:
            meta.tarball_info.board.append(baseboards[build_target])
        meta.file_name = tarball_name
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.EC  # pylint: disable=no-member
        )
        tarball_path = os.path.join(bundle_dir, elf_tarball_name)
        cmd = [
            "tar",
            r"--transform=s/.*\///",
            "-cvjf",
            tarball_path,
        ]
        cmd.extend(
            [
                str(x.relative_to(artifacts_dir))
                for x in artifacts_dir.glob("*/*.elf")
            ]
        )
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(
            cmd,
            cwd=artifacts_dir,
            check=True,
            stdin=subprocess.DEVNULL,
        )
        meta = info.objects.add()
        if build_target in baseboards and baseboards[build_target]:
            meta.tarball_info.board.append(baseboards[build_target])
        meta.file_name = elf_tarball_name
        meta.tarball_info.type = (
            firmware_pb2.FirmwareArtifactInfo.TarballInfo.FirmwareType.EC  # pylint: disable=no-member
        )
        # TODO(kmshelton): Populate the rest of metadata contents as it gets
        # defined in infra/proto/src/chromite/api/firmware.proto.

    write_metadata(opts, info)


def test(opts):
    """Runs all of the unit tests for EC firmware"""
    # TODO(b/169178847): Add appropriate metric information
    metrics = firmware_pb2.FwTestMetricList()  # pylint: disable=no-member
    with open(opts.metrics, "w", encoding="utf-8") as file:
        file.write(json_format.MessageToJson(metrics))

    # Run python unit tests.
    subprocess.run(
        ["extra/stack_analyzer/run_tests.sh"],
        cwd=os.path.dirname(__file__),
        check=True,
    )

    # If building for code coverage, build the 'coverage' target, which
    # builds the posix-based unit tests for code coverage and assembles
    # the LCOV information.
    #
    # Otherwise, build the 'runtests' target, which verifies all
    # posix-based unit tests build and pass.
    env = os.environ.copy()
    env.update(init_toolchain())
    target = "coverage" if opts.code_coverage else "runtests"
    cmd = ["make", target, f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)

    if not opts.code_coverage:
        # Verify compilation of the on-device unit test binaries.
        # TODO(b/172501728) These should build for all boards, but they've bit
        # rotted, so we only build the ones that compile.
        cmd = ["make", f"-j{opts.cpus}"]
        cmd.extend(["tests-" + b for b in BOARDS_UNIT_TEST])
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)

        # Verify the tests pass with ASan also
        ec_dir = os.path.dirname(__file__)
        build_dir = os.path.join(ec_dir, "build")
        host_dir = os.path.join(build_dir, "host")
        print(f"# Deleting {host_dir}")
        shutil.rmtree(host_dir)

        cmd = ["make", "TEST_ASAN=y", target, f"-j{opts.cpus}", "BOARD=host"]
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)

        print(f"# Deleting {host_dir}")
        shutil.rmtree(host_dir)

        # Use the x86_64-cros-linux-gnu- compiler also
        cmd = [
            "make",
            "HOST_CROSS_COMPILE=x86_64-cros-linux-gnu-",
            "TEST_ASAN=y",
            "BOARD=host",
            target,
            f"-j{opts.cpus}",
        ]
        print(f"# Running {' '.join(cmd)}.")
        subprocess.run(cmd, cwd=os.path.dirname(__file__), check=True, env=env)


def find_checkout():
    """Find the path to the base of the checkout (e.g., ~/chromiumos)."""
    for path in pathlib.Path(__file__).resolve().parents:
        if (path / ".repo").is_dir():
            return path
    raise FileNotFoundError("Unable to locate the root of the checkout")


def check_inherits(opts):
    """Reads the src/project/*/*/sw_build_config json files and compares
    the boards and ec targets.
    """

    env = os.environ.copy()
    env.update(init_toolchain())
    cmd = ["make", "print-boards", f"-j{opts.cpus}"]
    print(f"# Running {' '.join(cmd)}.")
    ec_boards = set()
    used_ec_boards = set()
    for line in subprocess.run(
        cmd,
        cwd=os.path.dirname(__file__),
        check=True,
        universal_newlines=True,
        stdout=subprocess.PIPE,
        env=env,
    ).stdout.splitlines():
        ec_boards.add(line)

    retcode = 0
    board_dirs = (find_checkout() / "src" / "project").glob("*")
    for board_dir in board_dirs:
        # Check the boxter sw_build_config files
        for cfg_path in board_dir.glob(
            "*/sw_build_config/platform/chromeos-config/generated/project-config.json"
        ):
            with open(cfg_path, "r", encoding="utf-8") as file:
                cfg = json.load(file)

                for software_config in cfg.get("chromeos", {}).get(
                    "configs", []
                ):
                    cros_ec = (
                        software_config.get("firmware", {})
                        .get("build-targets", {})
                        .get("ec", None)
                    )
                    if cros_ec:
                        if (
                            cros_ec not in ec_boards
                            and cros_ec not in EXPECTED_MISSING_BOARDS
                        ):
                            print(
                                f"ERROR: Unknown EC target {cros_ec} in {cfg_path}"
                            )
                            retcode = 1
                        used_ec_boards.add(cros_ec)
                    for cros_ec in (
                        software_config.get("firmware", {})
                        .get("build-targets", {})
                        .get("ec_extras", [])
                    ):
                        if cros_ec:
                            if (
                                cros_ec not in ec_boards
                                and cros_ec not in EXPECTED_MISSING_BOARDS
                            ):
                                print(
                                    f"ERROR: Unknown EC target (ec_extras) {cros_ec} in {cfg_path}"
                                )
                                retcode = 1
                            used_ec_boards.add(cros_ec)
        # Check the boxter joined.jsonproto files
        for cfg_path in board_dir.glob("*/generated/joined.jsonproto"):
            with open(cfg_path, "r", encoding="utf-8") as file:
                cfg = json.load(file)

                for software_config in cfg.get("softwareConfigs", []):
                    cros_ec = (
                        software_config.get("firmwareBuildConfig", {})
                        .get("buildTargets", {})
                        .get("ec", None)
                    )
                    if cros_ec:
                        if (
                            cros_ec not in ec_boards
                            and cros_ec not in EXPECTED_MISSING_BOARDS
                        ):
                            print(
                                f"ERROR: Unknown EC target {cros_ec} in {cfg_path}"
                            )
                            retcode = 1
                        used_ec_boards.add(cros_ec)
                    cros_ec = (
                        software_config.get("firmwareBuildConfig", {})
                        .get("buildTargets", {})
                        .get("ish", None)
                    )
                    if cros_ec:
                        if (
                            cros_ec not in ec_boards
                            and cros_ec not in EXPECTED_MISSING_BOARDS
                        ):
                            print(
                                f"ERROR: Unknown EC target (ISH) {cros_ec} in {cfg_path}"
                            )
                            retcode = 1
                        used_ec_boards.add(cros_ec)
                    cros_ec = (
                        software_config.get("firmwareBuildConfig", {})
                        .get("buildTargets", {})
                        .get("base", None)
                    )
                    if cros_ec:
                        if (
                            cros_ec not in ec_boards
                            and cros_ec not in EXPECTED_MISSING_BOARDS
                        ):
                            print(
                                f"ERROR: Unknown EC target (base) {cros_ec} in {cfg_path}"
                            )
                            retcode = 1
                        used_ec_boards.add(cros_ec)
                    for cros_ec in (
                        software_config.get("firmwareBuildConfig", {})
                        .get("buildTargets", {})
                        .get("ecExtras", [])
                    ):
                        if cros_ec:
                            if (
                                cros_ec not in ec_boards
                                and cros_ec not in EXPECTED_MISSING_BOARDS
                            ):
                                print(
                                    f"ERROR: Unknown EC target (ecExtras) {cros_ec} in {cfg_path}"
                                )
                                retcode = 1
                            used_ec_boards.add(cros_ec)
                for design in cfg.get("designList", []):
                    for config in design.get("configs", []):
                        cros_ec = (
                            software_config.get("hardwareFeatures", {})
                            .get("fingerprint", {})
                            .get("board", None)
                        )
                        if cros_ec:
                            if (
                                cros_ec not in ec_boards
                                and cros_ec not in EXPECTED_MISSING_BOARDS
                            ):
                                print(
                                    f"ERROR: Unknown EC target (FP) {cros_ec} in {cfg_path}"
                                )
                                retcode = 1
                            used_ec_boards.add(cros_ec)
    # Check the old yaml files
    # src/overlays/overlay-trogdor/chromeos-base/chromeos-config-bsp/files/model.yaml
    yamls = (find_checkout() / "src").glob(
        "*overlays/*/chromeos-base/chromeos-config-bsp*/files/model.yaml"
    )
    for yaml_path in yamls:
        with open(yaml_path, "r", encoding="utf-8") as file:
            try:
                for cfg in yaml.safe_load_all(file):
                    for device in cfg.get("chromeos", {}).get("devices", []):
                        for sku in device.get("skus", []):
                            config = sku.get("config", {})
                            cros_ec = (
                                config.get("firmware", {})
                                .get("build-targets", {})
                                .get("ec", None)
                            )
                            while cros_ec and cros_ec.startswith("{{$"):
                                lookup_key = cros_ec[2 : len(cros_ec) - 2]
                                if lookup_key in config:
                                    cros_ec = config.get(lookup_key)
                                elif lookup_key in sku:
                                    cros_ec = sku.get(lookup_key)
                                elif lookup_key in device:
                                    cros_ec = device.get(lookup_key)
                                else:
                                    print(
                                        f"ERROR: Failed to find {lookup_key} for"
                                        f" {sku} in {yaml_path}"
                                    )
                                    cros_ec = None
                            if cros_ec:
                                if (
                                    cros_ec not in ec_boards
                                    and cros_ec not in EXPECTED_MISSING_BOARDS
                                ):
                                    print(
                                        f"ERROR: Unknown EC target {cros_ec} in {yaml_path}"
                                    )
                                    retcode = 1
                                used_ec_boards.add(cros_ec)
            except yaml.composer.ComposerError:
                print(f"Ignoring {yaml_path}")
    for cros_ec in ec_boards:
        if (
            cros_ec not in used_ec_boards
            and cros_ec not in EXPECTED_UNUSED_BOARDS
        ):
            print(f"ERROR: EC target {cros_ec} is not used anywhere")
            retcode = 1

    return retcode


def main(args):
    """Builds, bundles, or tests all of the EC targets.

    Additionally, the tool reports build metrics.
    """
    parser, sub_cmds = scripts.firmware_builder_lib.create_arg_parser(
        build, bundle, test
    )
    check_inherits_cmd = sub_cmds.add_parser(
        "check_inherits",
        help="Checks the inherited_from values against Boxster",
    )
    check_inherits_cmd.set_defaults(func=check_inherits)

    opts = parser.parse_args(args)

    if not hasattr(opts, "func"):
        print("Must select a valid sub command!")
        return -1

    # Run selected sub command function
    try:
        opts.func(opts)
    except subprocess.CalledProcessError:
        ec_dir = os.path.dirname(__file__)
        failed_dir = os.path.join(ec_dir, ".failedboards")
        if os.path.isdir(failed_dir):
            print("Failed boards/tests:")
            for fail in os.listdir(failed_dir):
                print(f"\t{fail}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
