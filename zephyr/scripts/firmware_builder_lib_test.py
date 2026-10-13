# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for firmware_builder_lib."""

import pathlib
import shutil
import tempfile
import unittest

from scripts import firmware_builder_lib


class TestExportConfigs(unittest.TestCase):
    """Tests for export_configs function."""

    def setUp(self) -> None:
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        self.build_dir = pathlib.Path(tmp) / "build"
        self.bundle_dir = pathlib.Path(tmp) / "bundle"

    def _write(self, variant: str, name: str, text: str) -> None:
        """Writes a file of the build with the given variant name."""
        path = self.build_dir / f"build-{variant}" / "zephyr" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def _read(self, file_name: str) -> str:
        """Reads an exported file."""
        return (self.bundle_dir / file_name).read_text(encoding="utf-8")

    def test_exports_ro_and_rw_configs_and_rw_manifest(self) -> None:
        """Test that both configs and the manifest of the RW build are copied."""
        self._write("ro", ".config", "CONFIG_CROS_EC_RO=y\n")
        self._write("rw", ".config", "CONFIG_CROS_EC_RW=y\n")
        self._write("ro", "component_manifest.json", '{"from": "ro"}')
        self._write("rw", "component_manifest.json", '{"from": "rw"}')
        self.assertEqual(
            firmware_builder_lib.export_configs(
                "brox", ["ro", "rw"], self.build_dir, self.bundle_dir
            ),
            [
                ("configs/brox/ec-ro.config", "ec-ro"),
                ("configs/brox/ec-rw.config", "ec-rw"),
                ("configs/brox/components.json", "components"),
            ],
        )
        self.assertEqual(
            self._read("configs/brox/ec-ro.config"), "CONFIG_CROS_EC_RO=y\n"
        )
        self.assertEqual(
            self._read("configs/brox/ec-rw.config"), "CONFIG_CROS_EC_RW=y\n"
        )
        self.assertEqual(
            self._read("configs/brox/components.json"), '{"from": "rw"}'
        )

    def test_single_image_config_covers_both_regions(self) -> None:
        """Test that a single image has one config and no manifest."""
        self._write("singleimage", ".config", "CONFIG_FPU=y\n")
        self.assertEqual(
            firmware_builder_lib.export_configs(
                "bloonchipper", ["singleimage"], self.build_dir, self.bundle_dir
            ),
            [("configs/bloonchipper/ec.config", "ec")],
        )
        self.assertEqual(
            self._read("configs/bloonchipper/ec.config"), "CONFIG_FPU=y\n"
        )


if __name__ == "__main__":
    unittest.main()
