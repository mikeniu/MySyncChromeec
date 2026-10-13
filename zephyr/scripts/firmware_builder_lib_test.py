# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for firmware_builder_lib."""

import argparse
import pathlib
import shutil
import tempfile
import types
import unittest

from scripts import firmware_builder_lib


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


class TestExportConfigs(unittest.TestCase):
    """Tests for export_configs function."""

    def setUp(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        self.build_dir = pathlib.Path(tmp) / "build"
        self.bundle_dir = pathlib.Path(tmp) / "bundle"

    def _write(self, variant, name, text):
        """Writes a file of the build with the given variant name."""
        path = self.build_dir / f"build-{variant}" / "zephyr" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def _read(self, file_name):
        """Reads an exported file."""
        return (self.bundle_dir / file_name).read_text(encoding="utf-8")

    def test_exports_ro_and_rw_configs_and_rw_manifest(self):
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

    def test_single_image_config_covers_both_regions(self):
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
