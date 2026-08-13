#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for util/gen_test_matrix.py."""

import json
import pathlib
import sys
import tarfile
import tempfile
import unittest

UTIL_DIR = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(UTIL_DIR))

# pylint: disable=wrong-import-position
import gen_test_matrix


class TestGenTestMatrix(unittest.TestCase):
    """Tests for gen_test_matrix helper functions and generator."""

    def setUp(self):
        self.repo_dir = pathlib.Path("/mnt/host/source/src/platform/ec")

    def test_normalize_source_path(self):
        """Tests that paths are normalized to relative repo paths or filtered."""
        self.assertEqual(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec/common/crc.c", self.repo_dir
            ),
            "common/crc.c",
        )
        self.assertEqual(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec/zephyr/shim/src/cbi.c",
                self.repo_dir,
            ),
            "zephyr/shim/src/cbi.c",
        )
        self.assertEqual(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec-private/fingerprint/fpmcu.c",
                self.repo_dir,
            ),
            "fingerprint/fpmcu.c",
        )
        # Ignored paths
        self.assertIsNone(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec/build/zephyr/app.c",
                self.repo_dir,
            )
        )
        self.assertIsNone(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec/twister-out-host/cov.c",
                self.repo_dir,
            )
        )
        self.assertIsNone(
            gen_test_matrix.normalize_source_path(
                "/usr/include/stdio.h", self.repo_dir
            )
        )
        self.assertIsNone(
            gen_test_matrix.normalize_source_path(
                "/mnt/host/source/src/platform/ec/test/mock/mock.c",
                self.repo_dir,
            )
        )

    def test_parse_lcov_tracefile(self):
        """Tests parsing LCOV text stream into by_line and by_file maps."""
        sample_lcov = """TN:crc
SF:/mnt/host/source/src/platform/ec/common/crc.c
DA:10,5
DA:11,0
DA:12,3
end_of_record
TN:cbi_test
SF:/mnt/host/source/src/platform/ec/common/crc.c
DA:10,2
end_of_record
TN:cbi_test
SF:/mnt/host/source/src/platform/ec/zephyr/shim/src/cbi.c
DA:50,1
end_of_record
"""
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", delete=False
        ) as f:
            f.write(sample_lcov)
            temp_path = pathlib.Path(f.name)

        try:
            by_line, by_file = gen_test_matrix.parse_lcov_tracefile(
                temp_path, self.repo_dir
            )

            self.assertIn("common/crc.c", by_file)
            self.assertEqual(by_file["common/crc.c"], ["cbi_test", "crc"])
            self.assertIn("zephyr/shim/src/cbi.c", by_file)
            self.assertEqual(by_file["zephyr/shim/src/cbi.c"], ["cbi_test"])

            self.assertIn("common/crc.c", by_line)
            # Line 10 was hit by both crc and cbi_test
            self.assertEqual(by_line["common/crc.c"]["10"], ["cbi_test", "crc"])
            # Line 11 had 0 hits, so omitted
            self.assertNotIn("11", by_line["common/crc.c"])
            # Line 12 was hit only by crc
            self.assertEqual(by_line["common/crc.c"]["12"], ["crc"])
        finally:
            temp_path.unlink(missing_ok=True)

    def test_generate_test_matrix_from_info(self):
        """Tests end-to-end matrix generation from an lcov.info file."""
        sample_lcov = """TN:crc
SF:/mnt/host/source/src/platform/ec/common/crc.c
DA:10,1
end_of_record
"""
        with tempfile.TemporaryDirectory() as tmp_dir_name:
            tmp_dir = pathlib.Path(tmp_dir_name)
            info_file = tmp_dir / "lcov.info"
            output_json = tmp_dir / "test_matrix.json"

            info_file.write_text(sample_lcov, encoding="utf-8")

            ret = gen_test_matrix.generate_test_matrix(
                input_path=info_file,
                output_path=output_json,
                repo_dir=self.repo_dir,
            )
            self.assertEqual(ret, 0)
            self.assertTrue(output_json.exists())

            data = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(data.get("version"), 1)
            self.assertEqual(data.get("by_file"), {"common/crc.c": ["crc"]})
            self.assertEqual(
                data.get("by_line"), {"common/crc.c": {"10": ["crc"]}}
            )

    def test_generate_test_matrix_from_tarball(self):
        """Tests end-to-end matrix generation from a tarball archive."""
        sample_lcov = """TN:math_test
SF:/mnt/host/source/src/platform/ec/common/math_util.c
DA:20,3
end_of_record
"""
        with tempfile.TemporaryDirectory() as tmp_dir_name:
            tmp_dir = pathlib.Path(tmp_dir_name)
            info_file = tmp_dir / "lcov.info"
            tar_file = tmp_dir / "coverage.tbz2"
            output_json = tmp_dir / "test_matrix.json"

            info_file.write_text(sample_lcov, encoding="utf-8")
            with tarfile.open(tar_file, "w:bz2") as tar:
                tar.add(info_file, arcname="lcov.info")

            ret = gen_test_matrix.generate_test_matrix(
                input_path=tar_file,
                output_path=output_json,
                repo_dir=self.repo_dir,
            )
            self.assertEqual(ret, 0)
            self.assertTrue(output_json.exists())

            data = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(
                data.get("by_file"), {"common/math_util.c": ["math_test"]}
            )

    def test_missing_input_file(self):
        """Tests that missing input file returns an error."""
        with tempfile.TemporaryDirectory() as tmp_dir_name:
            tmp_dir = pathlib.Path(tmp_dir_name)
            ret = gen_test_matrix.generate_test_matrix(
                input_path=tmp_dir / "nonexistent.info",
                output_path=tmp_dir / "out.json",
                repo_dir=self.repo_dir,
            )
            self.assertEqual(ret, 1)


if __name__ == "__main__":
    unittest.main()
