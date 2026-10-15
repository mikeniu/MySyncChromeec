# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Test zmake modules module."""

import pathlib
import tempfile

# pylint:disable=import-error
import hypothesis
import hypothesis.strategies as st
import zmake.modules


module_lists = st.lists(
    st.one_of(*map(st.just, zmake.modules.known_modules)), unique=True
)


@hypothesis.given(module_lists)
@hypothesis.settings(deadline=None)
def test_locate_in_directory(modules):
    """Test the basic functionality of locate_from_directory"""

    with tempfile.TemporaryDirectory() as modules_dir:
        modules_dir = pathlib.Path(modules_dir).resolve()

        expected_modules = {}

        for module in modules:
            module_dir = modules_dir / module
            zephyr_dir = module_dir / "zephyr"
            zephyr_dir.mkdir(parents=True)

            module_yml = zephyr_dir / "module.yml"
            module_yml.write_bytes(b"")

            expected_modules[module] = module_dir

        assert (
            zmake.modules.locate_from_directory(modules_dir) == expected_modules
        )


def test_chre_module_default_with_internal(tmp_path, monkeypatch):
    """Test chre_module resolves to chre_internal when present by default."""
    monkeypatch.delenv("PLATFORM_EC_USE_INTERNAL_CHRE", raising=False)
    internal_dir = (
        tmp_path
        / "src"
        / "third_party"
        / "android"
        / "platform"
        / "system"
        / "chre_internal"
    )
    internal_dir.mkdir(parents=True)
    assert zmake.modules.chre_module("chre", tmp_path) == internal_dir


def test_chre_module_default_without_internal(tmp_path, monkeypatch):
    """Test chre_module falls back to chre when internal is missing."""
    monkeypatch.delenv("PLATFORM_EC_USE_INTERNAL_CHRE", raising=False)
    public_dir = (
        tmp_path
        / "src"
        / "third_party"
        / "android"
        / "platform"
        / "system"
        / "chre"
    )
    assert zmake.modules.chre_module("chre", tmp_path) == public_dir


def test_chre_module_env_override_disable(tmp_path, monkeypatch):
    """Test chre_module resolves to public repo when PLATFORM_EC_USE_INTERNAL_CHRE=0."""
    monkeypatch.setenv("PLATFORM_EC_USE_INTERNAL_CHRE", "0")
    internal_dir = (
        tmp_path
        / "src"
        / "third_party"
        / "android"
        / "platform"
        / "system"
        / "chre_internal"
    )
    internal_dir.mkdir(parents=True)
    public_dir = (
        tmp_path
        / "src"
        / "third_party"
        / "android"
        / "platform"
        / "system"
        / "chre"
    )
    assert zmake.modules.chre_module("chre", tmp_path) == public_dir
