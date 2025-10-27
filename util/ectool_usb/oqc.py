# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""A tool for OQC."""

import argparse
import csv
from datetime import datetime
from enum import IntEnum
from enum import StrEnum
import json
import os
import sys
import time

import communication
import ec_commands as commands


__version__ = "0.0.1"


class OqcTestError(IntEnum):
    """OQC test error codes."""

    SUCCESS = 0
    SW_INTEGRITY_PROBLEM = 1
    VENDOR_DATA_PROBLEM = 2
    WRONG_CHECKSUM = 3
    CONFIG_CHECK_FAILED = 4
    WRONG_PID = 5


class OqcTestStages(StrEnum):
    """OQC test stages."""

    PROGRAM_VERSION = "Program Version"
    CONFIG_FILE = "Config File"
    PRODUCTION_PLANT = "Production Plant"
    PROJECT_STEP = "Project Step"
    START_TIME = "Test Start Time"
    TEST_SEQUENCE = "Test Sequence"
    SERIAL = "Serial Number"
    VID = "VID"
    PID = "PID"
    RO_VERSION = "EC_RO Version"
    RW_VERSION = "EC_RW Version"
    VENDOR_DATA = "Fetching vendor data"
    CHECKSUM = "Checksum check"
    CONFIG_CHECK = "Config offsets check"
    FLASH = "Flash Read Write Test"
    COUNT = "Test Count"
    TIME = "Test Time(ms)"


class OqcTestResult(StrEnum):
    """OQC test results."""

    PASS = "PASS"
    FAIL = "FAIL"
    NO_TEST = "NO TEST"


EXPECTED_SIZE = 64
data_to_xor = [range(0x0, 0x35 + 1)]
check_sums = [
    (0x0B, [range(0x00, 0x0A + 1)]),
    (0x18, [range(0x0C, 0x17 + 1)]),
    (0x28, [range(0x1E, 0x27 + 1), range(0x29, 0x35 + 1)]),
    (0x36, [range(0x37, 0x3F + 1)]),
]

operations = {"and": lambda x, y: x & y, "equal": lambda x, y: x == y}


def _parse_int(value) -> int:
    return int(str(value), 0)


def _check_serial(comm, results) -> int:
    """Checks serial number."""
    results[OqcTestStages.SERIAL] = (comm.serial_number, OqcTestResult.PASS)
    return OqcTestError.SUCCESS


def _check_vid_pid(configuration, comm, results) -> int:
    """Checks VID and PID."""

    results[OqcTestStages.VID] = (f"{comm.vid:X}", OqcTestResult.PASS)

    if "pid" in configuration:
        expected_pid = _parse_int(configuration["pid"])
        if expected_pid != comm.pid:
            results[OqcTestStages.PID] = (
                f"Wrong VID: {comm.pid:X} expected: {expected_pid:X}",
                OqcTestResult.FAIL,
            )
            return OqcTestError.WRONG_PID

    results[OqcTestStages.PID] = (f"{comm.pid:X}", OqcTestResult.PASS)
    return OqcTestError.SUCCESS


def _check_versions(comm, results) -> int:
    """Checks RO and RW firmware versions."""
    get_ver = commands.GetVersionCmd1()
    ret = get_ver.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        results[OqcTestStages.RO_VERSION] = (
            "Failed to get version",
            OqcTestResult.FAIL,
        )
        results[OqcTestStages.RW_VERSION] = results[OqcTestStages.RO_VERSION]
        return OqcTestError.SW_INTEGRITY_PROBLEM
    if get_ver.response.curr_image != commands.ImageType.RW:
        results[OqcTestStages.RO_VERSION] = ("Not in RW", OqcTestResult.FAIL)
        results[OqcTestStages.RW_VERSION] = results[OqcTestStages.RO_VERSION]
        return OqcTestError.SW_INTEGRITY_PROBLEM

    ro_version = get_ver.response.ro_ver.decode(
        "ascii", errors="replace"
    ).rstrip("\x00")
    results[OqcTestStages.RO_VERSION] = (
        f"{ro_version}",
        OqcTestResult.PASS,
    )
    rw_version = get_ver.response.rw_ver.decode(
        "ascii", errors="replace"
    ).rstrip("\x00")
    results[OqcTestStages.RW_VERSION] = (
        f"{rw_version}",
        OqcTestResult.PASS,
    )
    return OqcTestError.SUCCESS


def _calculate_checksum(data, ranges) -> int:
    """Calculates the checksum of the given data."""
    check_sum = 0x00
    for data_range in ranges:
        for offset in data_range:
            check_sum += data[offset]
    return check_sum & 0xFF


def _check_vendor_data(configuration, comm, results) -> int:
    """Checks vendor specific data."""
    # get vendor specific data
    vendor_ec = commands.FpVendorCmd0(0)
    ret = vendor_ec.run(comm)
    if ret != commands.EcCommandResult.SUCCESS:
        results[OqcTestStages.VENDOR_DATA] = (
            "Failed to retrieve vendor specific data",
            OqcTestResult.FAIL,
        )
        return OqcTestError.VENDOR_DATA_PROBLEM
    if not vendor_ec.response or not vendor_ec.response.payload:
        results[OqcTestStages.VENDOR_DATA] = (
            "Empty vendor specific data",
            OqcTestResult.FAIL,
        )
        return OqcTestError.VENDOR_DATA_PROBLEM

    # check data size
    if len(vendor_ec.response.payload) != EXPECTED_SIZE:
        results[OqcTestStages.VENDOR_DATA] = (
            f"Wrong vendor specific data size {len(vendor_ec.response.payload)}",
            OqcTestResult.FAIL,
        )
        return OqcTestError.VENDOR_DATA_PROBLEM

    results[OqcTestStages.VENDOR_DATA] = (
        "Pass",
        OqcTestResult.PASS,
    )

    # apply XOR
    data = list(vendor_ec.response.payload)
    for data_range in data_to_xor:
        for offset in data_range:
            data[offset] ^= 0xFF

    # validate checksums
    for check_sum_offset, ranges in check_sums:
        check_sum = _calculate_checksum(data, ranges)
        if check_sum != data[check_sum_offset]:
            results[OqcTestStages.CHECKSUM] = (
                f"Wrong check sum ({check_sum:#x}) at {check_sum_offset:#x} for {ranges}",
                OqcTestResult.FAIL,
            )
            return OqcTestError.WRONG_CHECKSUM

    results[OqcTestStages.CHECKSUM] = (
        "Pass",
        OqcTestResult.PASS,
    )

    # verify against configuration file
    if "checks" in configuration:
        for c in configuration["checks"]:
            offset = _parse_int(c["offset"])
            size = _parse_int(c["size"])
            val = int.from_bytes(data[offset : offset + size])
            res = val
            for operation in c["operations"]:
                for op, x in operation.items():
                    res = operations[op](res, _parse_int(x))
            if not res:
                results[OqcTestStages.CONFIG_CHECK] = (
                    f"Check {c} failed for {val:#x}",
                    OqcTestResult.FAIL,
                )
                return OqcTestError.CONFIG_CHECK_FAILED

    results[OqcTestStages.CONFIG_CHECK] = (
        "Pass",
        OqcTestResult.PASS,
    )
    return OqcTestError.SUCCESS


def run_oqc(configuration_file, comm, results) -> int:
    """Runs all OQC checks."""

    start_time = time.perf_counter()

    results[OqcTestStages.PROGRAM_VERSION] = (__version__, OqcTestResult.PASS)
    results[OqcTestStages.CONFIG_FILE] = (
        configuration_file.name if configuration_file else "No file",
        OqcTestResult.PASS,
    )
    results[OqcTestStages.PRODUCTION_PLANT] = ("KR", OqcTestResult.PASS)
    results[OqcTestStages.PROJECT_STEP] = ("DV", OqcTestResult.PASS)
    results[OqcTestStages.START_TIME] = (
        datetime.now().strftime("%Y%m%d_%H%M%S"),
        OqcTestResult.PASS,
    )
    results[OqcTestStages.TEST_SEQUENCE] = ("1", OqcTestResult.PASS)
    results[OqcTestStages.FLASH] = ("No Flash test", OqcTestResult.PASS)
    results[OqcTestStages.COUNT] = (len(OqcTestStages), OqcTestResult.PASS)

    configuration = {}
    if configuration_file:
        configuration = json.load(configuration_file)

    checks = [
        lambda: _check_serial(comm, results),
        lambda: _check_vid_pid(configuration, comm, results),
        lambda: _check_versions(comm, results),
        lambda: _check_vendor_data(configuration, comm, results),
    ]

    end_time = time.perf_counter()

    results[OqcTestStages.TIME] = (
        f"{((end_time - start_time) * 1000):.2f}",
        OqcTestResult.PASS,
    )

    for check in checks:
        ret = check()
        if ret != OqcTestError.SUCCESS:
            return ret

    return OqcTestError.SUCCESS


def cmd_oqc(args) -> int:
    """Runs the OQC tests."""
    ret = 0
    results = {stage: ("N/A", OqcTestResult.NO_TEST) for stage in OqcTestStages}
    overall_result = OqcTestResult.FAIL
    try:
        with communication.UsbCommunication() as comm:
            ret = run_oqc(args.configuration_file, comm, results)
            overall_result = OqcTestResult.PASS
            for stage, (message, result) in results.items():
                print(f"{str(stage)} result {result} with {message}")
                if result != OqcTestResult.PASS:
                    overall_result = OqcTestResult.FAIL

    except communication.UsbCommunicationError as e:
        print(f"{e}")
    finally:
        with open(args.output, "a", newline="", encoding="utf-8") as csvfile:
            csv_writer = csv.writer(csvfile)
            if os.fstat(csvfile.fileno()).st_size == 0:
                csv_writer.writerow(list(results.keys()) + ["Overall result"])
            csv_writer.writerow(
                [comment for comment, _ in results.values()] + [overall_result]
            )

    return ret


def main() -> int:
    """Main function."""
    parser = argparse.ArgumentParser(
        description="Run OQC",
    )
    parser.set_defaults(func=cmd_oqc)
    parser.add_argument(
        "configuration_file", nargs="?", type=argparse.FileType("r")
    )
    parser.add_argument(
        "-o", "--output", default="oqc_results.csv", help="Output CSV file name"
    )
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
