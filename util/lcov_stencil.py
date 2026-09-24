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
import fnmatch
import logging
import re
import sys
from typing import Dict, List, Optional, Set


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
        "--exclude-pattern",
        "--exclude",
        "-r",
        dest="exclude_patterns",
        action="append",
        default=[],
        help="Glob pattern of files to exclude from coverage (like lcov -r)",
    )
    parser.add_argument(
        "--exclude-only",
        "--filter-only",
        action="store_true",
        help="Only exclude patterns from input files without stenciling lines",
    )
    parser.add_argument(
        "template_file",
        help=(
            "lcov info file to use as template (or input file if "
            "filtering only)"
        ),
    )
    parser.add_argument(
        "lcov_input",
        nargs="*",
        default=[],
        help="lcov info file to merge",
    )
    return parser.parse_args(argv)


def compile_exclude_patterns(
    patterns: List[str],
) -> Optional[re.Pattern]:
    """Compiles a list of glob patterns into a single regex."""
    if not patterns:
        return None
    regexes = [fnmatch.translate(p) for p in patterns]
    return re.compile("|".join(f"(?:{r})" for r in regexes))


def parse_template_file(
    filename: str, exclude_re: Optional[re.Pattern] = None
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
        for line in template_file:
            line = line.strip()
            if line == "end_of_record":
                file_name = None
            elif line.startswith(
                ("TN:", "FNDA:", "FNF:", "FNH:", "BRF:", "BRH:", "LF:", "LH:")
            ):
                pass
            elif line.startswith("SF:"):
                file_path = line[3:]
                if exclude_re and exclude_re.match(file_path):
                    for skip_l in template_file:
                        if skip_l.strip() == "end_of_record":
                            break
                    file_name = None
                    continue
                file_name = line
                if file_name not in data_by_path:
                    data_by_path[file_name] = set()
            elif (
                line.startswith("FN:")
                or line.startswith("DA:")
                or line.startswith("BRDA:")
            ):
                _directive, payload = line.split(":", 1)
                line_num = payload.split(",", 1)[0]
                if file_name and line_num:
                    data_by_path[file_name].add(line_num)
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
        for raw_line in input_file:
            line = raw_line.strip()
            if line.startswith("SF:"):
                target_lines = data_by_path.get(line)
                if not target_lines:
                    # Fast skip entire record if file is not in template
                    for skip_l in input_file:
                        if skip_l.strip() == "end_of_record":
                            break
                    continue

                record_lines = [raw_line]
                function_names = set()
                functions_found = 0
                functions_hit = 0
                lines_found = 0
                lines_hit = 0
                branches_found = 0
                branches_hit = 0
                should_write_record = False

                for rec_raw in input_file:
                    rec_line = rec_raw.strip()
                    if rec_line == "end_of_record":
                        if should_write_record:
                            record_lines.append(rec_raw)
                            output_file.write("".join(record_lines))
                        else:
                            logging.debug("Omitting record %s", line)
                        break

                    if rec_line.startswith("TN:"):
                        record_lines.append(rec_raw)
                    elif rec_line.startswith("FN:"):
                        fn_line, fn_name = rec_line.removeprefix("FN:").split(
                            ",", 1
                        )
                        if fn_line in target_lines:
                            record_lines.append(rec_raw)
                            functions_found += 1
                            should_write_record = True
                            function_names.add(fn_name)
                        else:
                            logging.debug("Omitting %s", rec_line)
                    elif rec_line.startswith("FNDA:"):
                        count, fn_name = rec_line.removeprefix("FNDA:").split(
                            ",", 1
                        )
                        if fn_name in function_names:
                            record_lines.append(rec_raw)
                            should_write_record = True
                            if count != "0":
                                functions_hit += 1
                        else:
                            logging.debug("Omitting %s", rec_line)
                    elif rec_line.startswith("DA:"):
                        da_line, count = rec_line.removeprefix("DA:").split(
                            ",", 1
                        )
                        if da_line in target_lines:
                            record_lines.append(rec_raw)
                            lines_found += 1
                            should_write_record = True
                            if count != "0":
                                lines_hit += 1
                        else:
                            logging.debug("Omitting %s", rec_line)
                    elif rec_line.startswith("BRDA:"):
                        # Format: BRDA:<line_num>,<block_num>,<branch_num>,<taken>
                        br_line, _block, _branch, taken = rec_line.removeprefix(
                            "BRDA:"
                        ).split(",")
                        if br_line in target_lines:
                            record_lines.append(rec_raw)
                            branches_found += 1
                            should_write_record = True
                            if taken not in ("-", "0"):
                                branches_hit += 1
                        else:
                            logging.debug("Omitting %s", rec_line)
                    elif rec_line.startswith("FNF:"):
                        record_lines.append(f"FNF:{functions_found}\n")
                    elif rec_line.startswith("FNH:"):
                        record_lines.append(f"FNH:{functions_hit}\n")
                    elif rec_line.startswith("BRF:"):
                        record_lines.append(f"BRF:{branches_found}\n")
                    elif rec_line.startswith("BRH:"):
                        record_lines.append(f"BRH:{branches_hit}\n")
                    elif rec_line.startswith("LF:"):
                        record_lines.append(f"LF:{lines_found}\n")
                    elif rec_line.startswith("LH:"):
                        record_lines.append(f"LH:{lines_hit}\n")
                    else:
                        logging.debug("record = %s", rec_line)
                        raise NotImplementedError(rec_line)


def exclude_patterns_from_file(
    filename: str, output_file, exclude_re: Optional[re.Pattern]
):
    """Streams records from filename to output_file, omitting records matching
    exclude_re.
    """
    logging.info("Filtering file %s", filename)
    if not exclude_re:
        with open(filename, "r", encoding="utf-8") as input_file:
            for line in input_file:
                output_file.write(line)
        return

    with open(filename, "r", encoding="utf-8") as input_file:
        record_lines = []
        skip_record = False
        for raw_line in input_file:
            line = raw_line.strip()
            if skip_record:
                if line == "end_of_record":
                    skip_record = False
                continue

            record_lines.append(raw_line)
            if line.startswith("SF:"):
                file_path = line[3:]
                if exclude_re.match(file_path):
                    skip_record = True
                    record_lines = []
                    continue
            elif line == "end_of_record":
                output_file.writelines(record_lines)
                record_lines = []

        if record_lines and not skip_record:
            output_file.writelines(record_lines)


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

    exclude_re = compile_exclude_patterns(opts.exclude_patterns)
    if opts.exclude_only or not opts.lcov_input:
        inputs = [opts.template_file] + opts.lcov_input
        with output_file:
            for lcov_input in inputs:
                exclude_patterns_from_file(lcov_input, output_file, exclude_re)
    else:
        data_by_path = parse_template_file(opts.template_file, exclude_re)
        with output_file:
            for lcov_input in [opts.template_file] + opts.lcov_input:
                filter_coverage_file(lcov_input, output_file, data_by_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
