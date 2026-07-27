#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Merge lcov files, discarding all lines that are not in the template file.

Given 2 or more lcov files, merge the results only for the lines present in
the template file.

File format reverse engineered from
https://github.com/linux-test-project/lcov/blob/master/bin/geninfo
"""

import argparse
from collections import OrderedDict
import logging
import re
import sys
from typing import Dict, Set, Tuple


EXTRACT_FN = re.compile(r"^FN:(\d+),(\S+)")
EXTRACT_FNDA = re.compile(r"^FNDA:(\d+),(\S+)")
EXTRACT_DA = re.compile(r"^DA:(\d+),(\d+)")
EXTRACT_BRDA = re.compile(r"^BRDA:(\d+),(\d+),(\d+),([-\d]+)")


class FileRecord:
    """Stores coverage data for a single source file."""

    def __init__(self, filename: str):
        self.filename = filename
        self.tn = ""
        self.stenciled_lines: Set[int] = set()
        self.stenciled_functions: Dict[str, int] = {}  # fn_name -> start_line
        self.stenciled_branches: Set[Tuple[int, int, int]] = (
            set()
        )  # (line, block, branch)

        self.line_hits: Dict[int, int] = {}
        self.function_hits: Dict[str, int] = {}
        self.branch_hits: Dict[Tuple[int, int, int], str] = {}


def parse_args(argv=None):
    """Parses command line args"""
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--log-level",
        choices=[
            "CRITICAL",
            "ERROR",
            "WARNING",
            "INFO",
            "DEBUG",
        ],
        default="INFO",
        help="Set logging level to report at.",
    )
    parser.add_argument(
        "--output-file",
        "-o",
        help="destination filename, defaults to stdout",
    )
    parser.add_argument(
        "template_file",
        help="lcov info file to use as template",
    )
    parser.add_argument(
        "lcov_input",
        nargs="+",
        help="lcov info file to merge",
    )
    return parser.parse_args(argv)


def parse_template(filename: str) -> OrderedDict:
    """Parses the template LCOV file into a dictionary of FileRecords."""
    logging.info("Reading template file %s", filename)
    records: OrderedDict[str, FileRecord] = OrderedDict()
    current_rec = None

    with open(filename, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line == "end_of_record":
                current_rec = None
            elif line.startswith("SF:"):
                sf_path = line[3:]
                if sf_path not in records:
                    records[sf_path] = FileRecord(sf_path)
                current_rec = records[sf_path]
            elif current_rec:
                if line.startswith("TN:"):
                    current_rec.tn = line[3:]
                elif line.startswith("FN:"):
                    m = EXTRACT_FN.match(line)
                    if m:
                        start_line = int(m.group(1))
                        fn_name = m.group(2)
                        current_rec.stenciled_functions[fn_name] = start_line
                elif line.startswith("FNDA:"):
                    m = EXTRACT_FNDA.match(line)
                    if m:
                        count = int(m.group(1))
                        fn_name = m.group(2)
                        current_rec.function_hits[fn_name] = (
                            current_rec.function_hits.get(fn_name, 0) + count
                        )
                elif line.startswith("DA:"):
                    m = EXTRACT_DA.match(line)
                    if m:
                        line_num = int(m.group(1))
                        count = int(m.group(2))
                        current_rec.stenciled_lines.add(line_num)
                        current_rec.line_hits[line_num] = max(
                            current_rec.line_hits.get(line_num, 0), count
                        )
                elif line.startswith("BRDA:"):
                    m = EXTRACT_BRDA.match(line)
                    if m:
                        l_num = int(m.group(1))
                        blk = int(m.group(2))
                        br = int(m.group(3))
                        cnt_str = m.group(4)
                        br_key = (l_num, blk, br)
                        current_rec.stenciled_branches.add(br_key)
                        current_rec.branch_hits[br_key] = cnt_str

    return records


def merge_input_files(lcov_inputs: list, records: OrderedDict):
    """Merges coverage data from input LCOV files into records."""
    for filename in lcov_inputs:
        logging.info("Merging file %s", filename)
        current_rec = None
        with open(filename, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line == "end_of_record":
                    current_rec = None
                elif line.startswith("SF:"):
                    sf_path = line[3:]
                    current_rec = records.get(sf_path)
                elif current_rec:
                    if line.startswith("FNDA:"):
                        m = EXTRACT_FNDA.match(line)
                        if m:
                            count = int(m.group(1))
                            fn_name = m.group(2)
                            if fn_name in current_rec.stenciled_functions:
                                current_rec.function_hits[fn_name] = (
                                    current_rec.function_hits.get(fn_name, 0)
                                    + count
                                )
                    elif line.startswith("DA:"):
                        m = EXTRACT_DA.match(line)
                        if m:
                            line_num = int(m.group(1))
                            count = int(m.group(2))
                            if line_num in current_rec.stenciled_lines:
                                current_rec.line_hits[line_num] = (
                                    current_rec.line_hits.get(line_num, 0)
                                    + count
                                )
                    elif line.startswith("BRDA:"):
                        m = EXTRACT_BRDA.match(line)
                        if m:
                            l_num = int(m.group(1))
                            blk = int(m.group(2))
                            br = int(m.group(3))
                            cnt_str = m.group(4)
                            br_key = (l_num, blk, br)
                            if br_key in current_rec.stenciled_branches:
                                prev = current_rec.branch_hits.get(br_key, "-")
                                if cnt_str != "-":
                                    if prev == "-":
                                        current_rec.branch_hits[br_key] = (
                                            cnt_str
                                        )
                                    else:
                                        current_rec.branch_hits[br_key] = str(
                                            int(prev) + int(cnt_str)
                                        )


def write_output(records: OrderedDict, output_file):
    """Writes the stenciled and merged LCOV records to output_file."""
    for sf_path, rec in records.items():
        output_file.write(f"TN:{rec.tn}\n")
        output_file.write(f"SF:{sf_path}\n")

        # Functions (FN and FNDA)
        for fn_name, start_line in sorted(
            rec.stenciled_functions.items(), key=lambda x: x[1]
        ):
            output_file.write(f"FN:{start_line},{fn_name}\n")
        for fn_name in sorted(rec.stenciled_functions.keys()):
            hits = rec.function_hits.get(fn_name, 0)
            output_file.write(f"FNDA:{hits},{fn_name}\n")

        fnf = len(rec.stenciled_functions)
        fnh = sum(
            1
            for fn_name in rec.stenciled_functions
            if rec.function_hits.get(fn_name, 0) > 0
        )
        output_file.write(f"FNF:{fnf}\n")
        output_file.write(f"FNH:{fnh}\n")

        # Lines (DA)
        sorted_lines = sorted(rec.stenciled_lines)
        for line_num in sorted_lines:
            hits = rec.line_hits.get(line_num, 0)
            output_file.write(f"DA:{line_num},{hits}\n")

        lf = len(sorted_lines)
        lh = sum(1 for l_num in sorted_lines if rec.line_hits.get(l_num, 0) > 0)
        output_file.write(f"LF:{lf}\n")
        output_file.write(f"LH:{lh}\n")

        # Branches (BRDA)
        sorted_branches = sorted(rec.stenciled_branches)
        for br_key in sorted_branches:
            cnt_str = rec.branch_hits.get(br_key, "-")
            output_file.write(
                f"BRDA:{br_key[0]},{br_key[1]},{br_key[2]},{cnt_str}\n"
            )

        brf = len(sorted_branches)
        brh = sum(
            1
            for br_key in sorted_branches
            if rec.branch_hits.get(br_key, "-") not in ("-", "0")
        )
        output_file.write(f"BRF:{brf}\n")
        output_file.write(f"BRH:{brh}\n")

        output_file.write("end_of_record\n")


def main(argv=None):
    """Merges lcov files."""
    opts = parse_args(argv)
    logging.basicConfig(level=opts.log_level)

    output_file = sys.stdout
    if opts.output_file:
        logging.info("Writing output to %s", opts.output_file)
        output_file = open(  # pylint:disable=consider-using-with
            opts.output_file, "w", encoding="utf-8"
        )

    records = parse_template(opts.template_file)
    merge_input_files(opts.lcov_input, records)

    with output_file:
        write_output(records, output_file)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
