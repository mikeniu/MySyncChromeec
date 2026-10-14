#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for FMAP and host tool helper utilities."""

import os
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock
from unittest.mock import patch

from flash.helpers import dump_fmap
from flash.helpers import FMAP_AREA_FORMAT
from flash.helpers import FMAP_AREA_PRESERVE
from flash.helpers import FMAP_HEADER_FORMAT
from flash.helpers import FMAP_SIGNATURE
from flash.helpers import get_preserve_ranges
from flash.helpers import get_writable_ranges
from flash.helpers import locate_tool
from flash.helpers import parse_fmap_areas
from flash.helpers import parse_fmap_bytes
from flash.helpers import preserve_sections_with_cur_image
from flash.helpers import resolve_image_writable_ranges
from flash.helpers import subtract_ranges


class TestHelpers(unittest.TestCase):
    """Tests for dump_fmap and preserve_sections_with_cur_image."""

    @patch("flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_success(
        self, mock_check_output: MagicMock, mock_locate: MagicMock
    ) -> None:
        """Return FMAP output when dump_fmap succeeds."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.return_value = "hit FMAP\n"
        ret = dump_fmap("fake.bin")
        mock_check_output.assert_called_once_with(
            ["/mock/dump_fmap", "-e", "fake.bin"], encoding="utf-8"
        )
        self.assertEqual(ret, "hit FMAP\n")

    @patch("flash.helpers.locate_tool")
    @patch("subprocess.check_output")
    def test_dump_fmap_failure(
        self, mock_check_output: MagicMock, mock_locate: MagicMock
    ) -> None:
        """Return empty string when dump_fmap raises CalledProcessError."""
        mock_locate.return_value = "/mock/dump_fmap"
        mock_check_output.side_effect = subprocess.CalledProcessError(
            1, ["dump_fmap"]
        )
        ret = dump_fmap("fake.bin")
        self.assertEqual(ret, "")

    @patch("flash.helpers.locate_tool", return_value="futility")
    @patch("os.path.exists")
    @patch("subprocess.run")
    @patch("shutil.move")
    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_with_cur_image(
        self,
        mock_dump_fmap: MagicMock,
        mock_move: MagicMock,
        mock_run: MagicMock,
        mock_exists: MagicMock,
        _mock_locate: MagicMock,
    ) -> None:
        """Dump and reload all FMAP sections marked as 'preserve'."""
        mock_exists.return_value = True
        mock_dump_fmap.return_value = (
            "RO_FRID 0x0 0x100 preserve\n"
            "RW_FWID 0x100 0x100 static,preserve\n"
        )
        temp_dir = "/tmp/fakedir"

        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", temp_dir
        )
        self.assertTrue(result)
        self.assertEqual(mock_run.call_count, 4)

        mock_run.assert_any_call(
            [
                "futility",
                "dump_fmap",
                "-x",
                os.path.abspath("cur.bin"),
                "RO_FRID",
            ],
            check=True,
            cwd=temp_dir,
        )
        mock_move.assert_any_call(
            os.path.join(temp_dir, "RO_FRID"),
            os.path.join(temp_dir, "ec.cur.RO_FRID.bin"),
        )
        mock_run.assert_any_call(
            [
                "futility",
                "load_fmap",
                os.path.abspath("new.bin"),
                f"RO_FRID:{os.path.join(temp_dir, 'ec.cur.RO_FRID.bin')}",
            ],
            check=True,
            cwd=temp_dir,
        )

    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_no_preserve(
        self, mock_dump_fmap: MagicMock
    ) -> None:
        """Return False when no FMAP sections are marked as 'preserve'."""
        mock_dump_fmap.return_value = "RO_FRID 0x0 0x100\n"
        result = preserve_sections_with_cur_image(
            "cur.bin", "new.bin", "/tmp/faketmp"
        )
        self.assertFalse(result)

    @patch("flash.helpers.locate_tool", return_value="futility")
    @patch("subprocess.run")
    @patch("flash.helpers.dump_fmap")
    def test_preserve_sections_futility_failure(
        self,
        mock_dump_fmap: MagicMock,
        mock_run: MagicMock,
        _mock_locate: MagicMock,
    ) -> None:
        """Skip the dump_fmap header and explain how to skip preservation."""
        mock_dump_fmap.return_value = (
            "name offset size flags\nCBI 0x0 0x100 preserve\n"
        )
        mock_run.side_effect = subprocess.CalledProcessError(1, "futility")
        with self.assertRaisesRegex(RuntimeError, "--no_preserve"):
            preserve_sections_with_cur_image("cur.bin", "new.bin", "/tmp/t")
        self.assertEqual(mock_run.call_args.args[0][-1], "CBI")

    @patch("shutil.which", return_value="/usr/bin/flashrom")
    def test_locate_tool_path_has_no_empty_entries(
        self, mock_which: MagicMock
    ) -> None:
        """Build PATH without empty entries when $PATH is unset."""
        with patch.dict(os.environ, {}, clear=True):
            locate_tool("flashrom", ec_dir="/ec")
        path = mock_which.call_args.kwargs["path"]
        self.assertEqual(path, "/ec/build/host/util")

    def _build_synthetic_fmap_image(self) -> bytes:
        """Build a 1 MB synthetic EC image containing WP_RO, CBI, and EC_RW."""
        total_size = 0x100000
        areas = [
            (0x00000, 0x60000, b"WP_RO", 0),
            (0x00000, 0x5E000, b"EC_RO", 0),
            (0x5E000, 0x01000, b"CBI", FMAP_AREA_PRESERVE),
            (0x80000, 0x80000, b"EC_RW", 0),
        ]
        header = struct.pack(
            FMAP_HEADER_FORMAT,
            FMAP_SIGNATURE,
            1,
            1,
            0,
            total_size,
            b"EC_FMAP",
            len(areas),
        )
        entries = b"".join(
            struct.pack(FMAP_AREA_FORMAT, off, sz, name, flags)
            for off, sz, name, flags in areas
        )
        fmap_blob = header + entries
        image = bytearray(b"\xff" * total_size)
        image[0x1000 : 0x1000 + len(fmap_blob)] = fmap_blob
        return bytes(image)

    def test_parse_fmap_bytes_and_writable_ranges(self) -> None:
        """Parse binary __FMAP__ and exclude CBI from writable ranges."""
        img_bytes = self._build_synthetic_fmap_image()
        areas = parse_fmap_bytes(img_bytes)
        self.assertIn("CBI", areas)
        self.assertTrue(areas["CBI"].is_preserve)
        self.assertEqual(areas["CBI"].offset, 0x5E000)
        self.assertEqual(areas["CBI"].size, 0x1000)

        ro_slice = img_bytes[:0x60000]
        ro_areas = parse_fmap_bytes(ro_slice)
        self.assertIn("EC_RW", ro_areas)
        self.assertIn("CBI", ro_areas)

        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(img_bytes)
            tmp.flush()

            self.assertEqual(
                get_preserve_ranges(tmp.name),
                [(0x5E000, 0x5F000)],
            )
            _areas, resolved_size, resolved_ranges = (
                resolve_image_writable_ranges(tmp.name, no_preserve=False)
            )
            self.assertEqual(resolved_size, 0x100000)
            self.assertEqual(
                resolved_ranges,
                [(0x0, 0x5E000), (0x5F000, 0x100000)],
            )
            self.assertEqual(
                get_writable_ranges(tmp.name, 0x100000, no_preserve=False),
                [(0x0, 0x5E000), (0x5F000, 0x100000)],
            )
            self.assertEqual(
                get_writable_ranges(
                    tmp.name,
                    0x100000,
                    target_regions=["WP_RO", "EC_RW"],
                    no_preserve=False,
                ),
                [(0x0, 0x5E000), (0x5F000, 0x60000), (0x80000, 0x100000)],
            )
            self.assertEqual(
                get_writable_ranges(
                    tmp.name,
                    0x100000,
                    target_regions=["EC_RO"],
                    no_preserve=False,
                ),
                [(0x0, 0x5E000)],
            )
            self.assertEqual(
                get_writable_ranges(
                    tmp.name,
                    0x100000,
                    target_regions=["EC_RW"],
                    no_preserve=False,
                ),
                [(0x80000, 0x100000)],
            )
            self.assertEqual(
                get_writable_ranges(tmp.name, 0x100000, no_preserve=True),
                [(0x0, 0x100000)],
            )

    def test_writable_ranges_reject_missing_regions(self) -> None:
        """Raise when a requested region is not in the image FMAP."""
        with self.assertRaisesRegex(ValueError, "no EC_RW area"):
            get_writable_ranges("unused.bin", 0x100000, ["EC_RW"], areas={})
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(b"\xff" * 0x1000)
            tmp.flush()
            with patch("flash.helpers.dump_fmap", return_value=""):
                with self.assertRaisesRegex(ValueError, "no EC_RO area"):
                    resolve_image_writable_ranges(tmp.name, ro_only=True)
                with self.assertRaisesRegex(
                    ValueError, "Cannot specify both ro_only and rw_only"
                ):
                    resolve_image_writable_ranges(
                        tmp.name, ro_only=True, rw_only=True
                    )
                self.assertEqual(
                    resolve_image_writable_ranges(tmp.name), ({}, 0, [])
                )

    @patch("flash.helpers.dump_fmap")
    def test_parse_fmap_areas_dump_fmap_fallback(
        self, mock_dump_fmap: MagicMock
    ) -> None:
        """Fall back to dump_fmap output when binary __FMAP__ is absent."""
        mock_dump_fmap.return_value = (
            "WP_RO 0x0 0x40000 static\n"
            "CBI 0x3e000 0x1000 static\n"
            "EC_RW 0x40000 0x40000 static\n"
        )
        areas = parse_fmap_areas("nonexistent_image.bin")
        self.assertTrue(areas["CBI"].is_preserve)
        self.assertEqual(
            subtract_ranges([(0, 0x80000)], [(0x3E000, 0x3F000)]),
            [(0, 0x3E000), (0x3F000, 0x80000)],
        )

    @patch("flash.helpers.dump_fmap")
    def test_parse_fmap_areas_falls_back_when_no_valid_areas(
        self, mock_dump_fmap: MagicMock
    ) -> None:
        """Fall back to dump_fmap when file exists but has no valid areas."""
        mock_dump_fmap.return_value = "WP_RO 0x0 0x40000 static\n"
        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(b"no fmap header in this file")
            tmp.flush()
            areas = parse_fmap_areas(tmp.name)
            self.assertIn("WP_RO", areas)
            mock_dump_fmap.assert_called_once_with(tmp.name)


if __name__ == "__main__":
    unittest.main()
