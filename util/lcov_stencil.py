#!/usr/bin/env python3
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Merge lcov files, discarding all lines that are not in the template file.

Given 2 or more lcov files, merge the results only for the lines present in
the template file and omit records matching exclude patterns.

File format reverse engineered from
https://github.com/linux-test-project/lcov/blob/master/bin/geninfo
"""

import argparse
from collections import defaultdict
import fnmatch
import logging
import re
import sys
from typing import Dict, List, Pattern, Set


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
        "--exclude",
        "-r",
        action="append",
        default=[],
        help="Exclude file patterns matching glob (can be specified multiple times)",
    )
    parser.add_argument(
        "template_file",
        help="lcov info file to use as template",
    )
    parser.add_argument(
        "lcov_input",
        nargs="*",
        default=[],
        help="lcov info file(s) to merge",
    )
    return parser.parse_args(argv)


def compile_exclude_patterns(patterns: List[str]) -> List[Pattern]:
    """Compiles glob patterns to regexes for fast matching."""
    compiled = []
    for p in patterns:
        compiled.append(re.compile(fnmatch.translate(p)))
    return compiled


def is_file_excluded(file_path: str, exclude_regexes: List[Pattern]) -> bool:
    """Returns True if file_path matches any of the exclude patterns."""
    for regex in exclude_regexes:
        if regex.match(file_path):
            return True
    return False


def parse_template_file(
    filename, exclude_regexes: List[Pattern] = None
) -> Dict[str, Set[str]]:
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
        is_excluded = False
        for line in template_file.readlines():
            line = line.strip()
            if line == "end_of_record":
                file_name = None
                is_excluded = False
            elif line.startswith("SF:"):
                file_name = line
                path_part = line[3:]
                if exclude_regexes and is_file_excluded(
                    path_part, exclude_regexes
                ):
                    is_excluded = True
            elif is_excluded:
                continue
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
            else:
                match = EXTRACT_LINE.match(line)
                if file_name and match:
                    data_by_path[file_name].add(match.group(2))
                else:
                    raise NotImplementedError(line)
        return data_by_path


def filter_coverage_file(
    filename, output_file, data_by_path, exclude_regexes: List[Pattern] = None
):
    """Reads a coverage file from filename and writes filtered lines to
    output_file.

    For each line in filename, if it covers the same lines as the template
    in data_by_path, then write the line to output_file.

    Directives that act as totals (FNF, FNH, BRF, BRH, LF, LH) are recalculated
    after filtering, and records that refer to unknown files are omitted.
    """
    logging.info("Merging file %s", filename)
    with open(filename, "r", encoding="utf-8") as input_file:

        def empty_record():
            return {
                "text": "",
                "function_names": set(),
            }

        record = empty_record()
        is_excluded = False
        for line in input_file.readlines():
            line = line.strip()
            if line == "end_of_record":
                record["text"] += line + "\n"
                if record.get("should_write_record", False) and not is_excluded:
                    output_file.write(record["text"])
                else:
                    logging.debug("Omitting record %s", record["text"])
                record = empty_record()
                is_excluded = False
            elif line.startswith("SF:"):
                record["file_name"] = line
                record["text"] += line + "\n"
                path_part = line[3:]
                if exclude_regexes and is_file_excluded(
                    path_part, exclude_regexes
                ):
                    is_excluded = True
            elif is_excluded:
                continue
            elif line.startswith("TN:"):
                record["text"] += line + "\n"
            elif line.startswith("FN:"):
                match = EXTRACT_FN.match(line)
                if (
                    match
                    and match.group(2) in data_by_path[record["file_name"]]
                ):
                    record["text"] += line + "\n"
                    record["functions_found"] = (
                        record.get("functions_found", 0) + 1
                    )
                    record["should_write_record"] = True
                    record["function_names"].add(match.group(3))
                else:
                    logging.debug("Omitting %s", line)
            elif line.startswith("FNDA:"):
                match = EXTRACT_FNDA.match(line)
                if match and match.group(3) in record["function_names"]:
                    record["text"] += line + "\n"
                    record["should_write_record"] = True
                    if match.group(2) != "0":
                        record["functions_hit"] = (
                            record.get("functions_hit", 0) + 1
                        )
                else:
                    logging.debug("Omitting %s", line)
            elif line.startswith("DA:"):
                match = EXTRACT_DA.match(line)
                if (
                    match
                    and match.group(2) in data_by_path[record["file_name"]]
                ):
                    record["text"] += line + "\n"
                    record["lines_found"] = record.get("lines_found", 0) + 1
                    record["should_write_record"] = True
                    if match.group(3) != "0":
                        record["lines_hit"] = record.get("lines_hit", 0) + 1
                else:
                    logging.debug("Omitting %s", line)
            elif line.startswith("BRDA:"):
                match = EXTRACT_BRDA.match(line)
                if (
                    match
                    and match.group(2) in data_by_path[record["file_name"]]
                ):
                    record["text"] += line + "\n"
                    record["branches_found"] = (
                        record.get("branches_found", 0) + 1
                    )
                    record["should_write_record"] = True
                    if match.group(4) != "-" and match.group(4) != "0":
                        record["branches_hit"] = (
                            record.get("branches_hit", 0) + 1
                        )
                else:
                    logging.debug("Omitting %s", line)
            elif line.startswith("FNF:"):
                record["text"] += f"FNF:{record.get('functions_found', 0)}\n"
            elif line.startswith("FNH:"):
                record["text"] += f"FNH:{record.get('functions_hit', 0)}\n"
            elif line.startswith("BRF:"):
                record["text"] += f"BRF:{record.get('branches_found', 0)}\n"
            elif line.startswith("BRH:"):
                record["text"] += f"BRH:{record.get('branches_hit', 0)}\n"
            elif line.startswith("LF:"):
                record["text"] += f"LF:{record.get('lines_found', 0)}\n"
            elif line.startswith("LH:"):
                record["text"] += f"LH:{record.get('lines_hit', 0)}\n"
            else:
                logging.debug("record = %s", record)
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

    exclude_patterns = []
    for arg in opts.exclude:
        for p in arg.split():
            exclude_patterns.append(p)

    exclude_regexes = compile_exclude_patterns(exclude_patterns)
    data_by_path = parse_template_file(opts.template_file, exclude_regexes)

    with output_file:
        input_files = [opts.template_file]
        if opts.lcov_input:
            input_files.extend(opts.lcov_input)
        for lcov_input in input_files:
            filter_coverage_file(
                lcov_input, output_file, data_by_path, exclude_regexes
            )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
