# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for firmware_builder_lib."""

import argparse
import pathlib
import re
import subprocess
import tempfile
import types
import unittest
from unittest import mock

from scripts import firmware_builder_lib


# The unit tests (TN:) and the builds of the boards, as merged by lcov -a. The
# boards only have the lines that they compile, without hits.
MERGED_LCOV = """\
TN:
SF:/ec/common/a.c
DA:1,5
DA:2,0
DA:3,1
DA:4,0
end_of_record
TN:
SF:/ec/test/a_test.c
DA:1,2
DA:2,0
end_of_record
TN:
SF:/ec/third_party/lib.c
DA:1,3
end_of_record
TN:board_x
SF:/ec/common/a.c
DA:1,0
DA:2,0
end_of_record
TN:board_x
SF:/ec/third_party/lib.c
DA:1,0
DA:7,0
end_of_record
TN:board_y
SF:/ec/common/a.c
DA:3,0
end_of_record
"""


def _create_mock_project(name: str) -> types.SimpleNamespace:
    """Creates a mock project object mimicking zmake.project.Project."""
    return types.SimpleNamespace(
        config=types.SimpleNamespace(project_name=name)
    )


class TestShardProjects(unittest.TestCase):
    """Tests for shard_projects function."""

    def test_sorts_and_shards_project_objects(self):
        """Test sorting and sharding project objects with config.project_name."""
        projects = [
            _create_mock_project("skyrim"),
            _create_mock_project("brya"),
            _create_mock_project("corsola"),
            _create_mock_project("dedede"),
        ]
        opts = argparse.Namespace(shard_count=2, shard_index=1)
        result1 = firmware_builder_lib.shard_projects(list(projects), opts)
        self.assertEqual(
            [p.config.project_name for p in result1], ["brya", "corsola"]
        )

        opts.shard_index = 2
        result2 = firmware_builder_lib.shard_projects(list(projects), opts)
        self.assertEqual(
            [p.config.project_name for p in result2], ["dedede", "skyrim"]
        )

    def test_sorts_and_shards_string_board_names(self):
        """Test sorting and sharding iterable of string board names."""
        boards = ["sanok", "bloonchipper", "helipilot", "chudow"]
        opts = argparse.Namespace(shard_count=4, shard_index=1)
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), opts),
            ["bloonchipper"],
        )

        opts.shard_index = 2
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), opts),
            ["chudow"],
        )

        opts.shard_index = 3
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), opts),
            ["helipilot"],
        )

        opts.shard_index = 4
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), opts),
            ["sanok"],
        )

    def test_unsharded_preserves_all_boards(self):
        """Test that missing opts or unset shard flags preserves all boards."""
        boards = ["sanok", "bloonchipper", "helipilot", "chudow"]
        # None opts
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), None),
            ["bloonchipper", "chudow", "helipilot", "sanok"],
        )

        # Unset shard flags
        opts = argparse.Namespace(shard_count=None, shard_index=None)
        self.assertEqual(
            firmware_builder_lib.shard_projects(list(boards), opts),
            ["bloonchipper", "chudow", "helipilot", "sanok"],
        )

    def test_invalid_shard_index_raises(self):
        """Test that invalid shard index bounds raise ValueError."""
        boards = ["a", "b"]
        opts = argparse.Namespace(shard_count=2, shard_index=3)
        with self.assertRaises(ValueError):
            firmware_builder_lib.shard_projects(list(boards), opts)

        opts = argparse.Namespace(shard_count=2, shard_index=0)
        with self.assertRaises(ValueError):
            firmware_builder_lib.shard_projects(list(boards), opts)


class TestSummarizeMergedLcov(unittest.TestCase):
    """Tests for summarize_merged_lcov function."""

    def _summarize(self, boards):
        """Runs summarize_merged_lcov on MERGED_LCOV."""
        with tempfile.NamedTemporaryFile("w+", encoding="utf-8") as lcov_info:
            lcov_info.write(MERGED_LCOV)
            lcov_info.flush()
            return firmware_builder_lib.summarize_merged_lcov(
                lcov_info.name,
                boards,
                re.compile(r".*/test/"),
                re.compile(r".*/(test|third_party)/"),
            )

    def test_metrics(self):
        """Test the metrics of the unit tests and of the boards."""
        self.assertEqual(
            self._summarize(["board-x", "board-y"]),
            {
                "ALL_MERGED": (4, 8),
                "ALL_FILTERED": (3, 6),
                # Only the lines that the board compiles, and not those of the
                # excluded files, are counted. Hits of any test name count.
                "BOARD_BOARD-X": (1, 2),
                "BOARD_BOARD-Y": (1, 1),
            },
        )

    def test_board_without_lines_raises(self):
        """Test that a board that is not in the tracefile raises ValueError."""
        with self.assertRaisesRegex(ValueError, "BOARD_BOARD-Z"):
            self._summarize(["board-z"])


class TestExtractLcovSummaryWorker(unittest.TestCase):
    """Tests for extract_lcov_summary_worker function."""

    @mock.patch.object(subprocess, "run")
    def test_extracts_summary(self, mock_run):
        """Test parsing valid lcov --summary output."""
        mock_run.return_value = types.SimpleNamespace(
            stdout=(
                "Reading tracefile /tmp/lcov.info\n"
                "Summary coverage rate:\n"
                "  lines......: 84.5% (4225 of 5000 lines)\n"
                "  functions..: 91.2% (310 of 340 functions)\n"
            )
        )
        logged_cmds = []
        zephyr_dir = pathlib.Path("/tmp/zephyr")

        result = firmware_builder_lib.extract_lcov_summary_worker(
            "ALL_MERGED",
            pathlib.Path("/tmp/lcov.info"),
            zephyr_dir=zephyr_dir,
            log_cmd_fn=logged_cmds.append,
        )

        self.assertEqual(result, ("ALL_MERGED", 84.5, 4225, 5000))
        self.assertEqual(len(logged_cmds), 1)
        mock_run.assert_called_once_with(
            [
                "/usr/bin/lcov",
                "--ignore-errors",
                "inconsistent",
                "--summary",
                pathlib.Path("/tmp/lcov.info"),
            ],
            cwd=zephyr_dir,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
            stdin=subprocess.DEVNULL,
        )

    @mock.patch.object(subprocess, "run")
    def test_invalid_output_raises(self, mock_run):
        """Test that unparsable lcov output raises ValueError."""
        mock_run.return_value = types.SimpleNamespace(
            stdout="Summary coverage rate:\n  functions..: 91.2%\n"
        )

        with self.assertRaisesRegex(
            ValueError, "Failed to parse LCOV summary output for ALL_MERGED"
        ):
            firmware_builder_lib.extract_lcov_summary_worker(
                "ALL_MERGED", pathlib.Path("/tmp/lcov.info")
            )


if __name__ == "__main__":
    unittest.main()
