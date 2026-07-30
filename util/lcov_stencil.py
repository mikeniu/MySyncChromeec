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
from collections import defaultdict
import logging
import re
import sys
from typing import Dict, Set


EXTRACT_LINE = re.compile(r"^(FN|DA|BRDA):(\d+),")
EXTRACT_FN = re.compile(r"^(FN):(\d+),(\S+)")
EXTRACT_FNDA = re.compile(r"^(FNDA):(\d+),(\S+)")
EXTRACT_DA = re.compile(r"^(DA):(\d+),(\d+)")
EXTRACT_BRDA = re.compile(r"^(BRDA):(\d+),(\d+),(\d+),([-\d]+)")
EXTRACT_COUNT = re.compile(r"^([A-Z]+):(\d+)")


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


def parse_template_file(filename) -> Dict[str, Set[str]]:
    """Reads the template file and returns covered lines.

    Reads the lines that indicate covered line numbers (FN, DA, and BRDA)
    and adds them to the returned data structure.

    Returns
    -------
    Dict[str, Set[str]]
        A dictionary of filename to set of covered line numbers (as strings)
    """
    logging.info("Reading template file %s", filename)
    with open(filename, "r", encoding="utf-8") as template_file:
        data_by_path: Dict[str, Set[str]] = defaultdict(set)
        file_name = None
        for line in template_file:
            line = line.strip()
            if line == "end_of_record":
                file_name = None
            elif (
                line.startswith(  # pylint:disable=too-many-boolean-expressions
                    "TN:"
                )
                or line.startswith("FNDA:")
                or line.startswith("FNF:")
                or line.startswith("FNH:")
                or line.startswith("BRF:")
                or line.startswith("BRH:")
                or line.startswith("LF:")
                or line.startswith("LH:")
            ):
                pass
            elif line.startswith("SF:"):
                file_name = line
            elif file_name:
                if line.startswith(("DA:", "FN:", "BRDA:")):
                    colon_pos = line.find(":")
                    comma_pos = line.find(",", colon_pos)
                    if colon_pos != -1 and comma_pos != -1:
                        data_by_path[file_name].add(
                            line[colon_pos + 1 : comma_pos]
                        )
                    else:
                        raise NotImplementedError(line)
                else:
                    raise NotImplementedError(line)
            else:
                raise NotImplementedError(line)
        return data_by_path


def filter_coverage_file(filename, output_file, data_by_path):
    """Reads a coverage file from filename and writes filtered lines to
    output_file.

    For each line in filename, if it covers the same lines as the template
    in data_by_path, then write the line to output_file.

    Directives that act as totals (FNF, FNH, BRF, BRH, LF, LH) are recalculated
    after filtering, and records that refer to unknown files are omitted.
    """
    logging.info("Merging file %s", filename)
    with open(filename, "r", encoding="utf-8") as input_file:
        lines_buf = []
        function_names = set()
        file_name = None
        should_write = False
        functions_found = 0
        functions_hit = 0
        lines_found = 0
        lines_hit = 0
        branches_found = 0
        branches_hit = 0

        for line in input_file:
            line = line.strip()
            if line == "end_of_record":
                if should_write:
                    output_file.write("".join(lines_buf))
                    output_file.write("end_of_record\n")
                lines_buf = []
                function_names = set()
                file_name = None
                should_write = False
                functions_found = functions_hit = lines_found = lines_hit = (
                    branches_found
                ) = branches_hit = 0
            elif line.startswith("SF:"):
                file_name = line
                lines_buf.append(line + "\n")
            elif line.startswith("TN:"):
                lines_buf.append(line + "\n")
            elif line.startswith("FN:"):
                comma_pos = line.find(",", 3)
                if comma_pos != -1:
                    line_num = line[3:comma_pos]
                    fn_name = line[comma_pos + 1 :]
                    if file_name and line_num in data_by_path[file_name]:
                        lines_buf.append(line + "\n")
                        functions_found += 1
                        should_write = True
                        function_names.add(fn_name)
                    else:
                        logging.debug("Omitting %s", line)
                else:
                    raise NotImplementedError(line)
            elif line.startswith("FNDA:"):
                comma_pos = line.find(",", 5)
                if comma_pos != -1:
                    fn_count = line[5:comma_pos]
                    fn_name = line[comma_pos + 1 :]
                    if fn_name in function_names:
                        lines_buf.append(line + "\n")
                        should_write = True
                        if fn_count != "0":
                            functions_hit += 1
                    else:
                        logging.debug("Omitting %s", line)
                else:
                    raise NotImplementedError(line)
            elif line.startswith("DA:"):
                comma_pos = line.find(",", 3)
                if comma_pos != -1:
                    line_num = line[3:comma_pos]
                    line_count = line[comma_pos + 1 :]
                    if file_name and line_num in data_by_path[file_name]:
                        lines_buf.append(line + "\n")
                        lines_found += 1
                        should_write = True
                        if line_count != "0":
                            lines_hit += 1
                    else:
                        logging.debug("Omitting %s", line)
                else:
                    raise NotImplementedError(line)
            elif line.startswith("BRDA:"):
                parts = line.split(",")
                if len(parts) >= 4:
                    line_num = parts[0][5:]
                    count = parts[3]
                    if file_name and line_num in data_by_path[file_name]:
                        lines_buf.append(line + "\n")
                        branches_found += 1
                        should_write = True
                        if count not in ("-", "0"):
                            branches_hit += 1
                    else:
                        logging.debug("Omitting %s", line)
                else:
                    raise NotImplementedError(line)
            elif line.startswith("FNF:"):
                lines_buf.append(f"FNF:{functions_found}\n")
            elif line.startswith("FNH:"):
                lines_buf.append(f"FNH:{functions_hit}\n")
            elif line.startswith("BRF:"):
                lines_buf.append(f"BRF:{branches_found}\n")
            elif line.startswith("BRH:"):
                lines_buf.append(f"BRH:{branches_hit}\n")
            elif line.startswith("LF:"):
                lines_buf.append(f"LF:{lines_found}\n")
            elif line.startswith("LH:"):
                lines_buf.append(f"LH:{lines_hit}\n")
            else:
                logging.debug("record = %s", line)
                raise NotImplementedError(line)


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

    data_by_path = parse_template_file(opts.template_file)
    with output_file:
        for lcov_input in [opts.template_file] + opts.lcov_input:
            filter_coverage_file(lcov_input, output_file, data_by_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
