# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake main entry point and integration with codebase prep."""

import pathlib
from unittest import mock

import pytest  # pylint: disable=import-error
import zmake.__main__ as main


@pytest.fixture(name="mock_zmake_class")
def fixture_mock_zmake_class():
    """Mock the Zmake class."""
    with mock.patch("zmake.zmake.Zmake", autospec=True) as mocked:
        # Mock the instance returned
        instance = mocked.return_value
        instance.executor = mock.Mock()
        instance.executor.wait.return_value = 0
        instance.cmp_failed_projects = {}
        instance.failed_projects = []
        yield mocked


@pytest.fixture(name="mock_preparer")
def fixture_mock_preparer():
    """Mock the CodebasePreparer class."""
    with mock.patch("zmake.__main__.CodebasePreparer", autospec=True) as mocked:
        yield mocked


@pytest.fixture(name="mock_locate_checkout")
def fixture_mock_locate_checkout():
    """Mock locate_cros_checkout to return a fake path."""
    with mock.patch(
        "zmake.zmake.util.locate_cros_checkout", autospec=True
    ) as mocked:
        mocked.return_value = pathlib.Path("/fake/checkout")
        yield mocked


@pytest.fixture(name="clean_env")
def fixture_clean_env(monkeypatch):
    """Ensure ZMAKE_WORKSPACE_PREPARED is not set."""
    monkeypatch.delenv("ZMAKE_WORKSPACE_PREPARED", raising=False)


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_build_triggers_prep(mock_preparer):
    """Test that 'zmake build' triggers codebase preparation in-place by default (no files)."""
    with mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        main.main(["build", "project1"])

    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        commit=False,
    )
    # Verify context manager was used
    preparer_instance = mock_preparer.return_value
    preparer_instance.__enter__.assert_called_once()
    preparer_instance.__exit__.assert_called_once()


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_list_projects_does_not_trigger_prep(mock_preparer):
    """Test that 'zmake list-projects' does not trigger codebase preparation."""
    main.main(["list-projects"])
    mock_preparer.assert_not_called()


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_custom_dirs(mock_preparer):
    """Test passing custom patches and override directories."""
    main.main(
        [
            "--patches-dir",
            "/custom/patches1",
            "--patches-dir",
            "/custom/patches2",
            "--src-override-dir",
            "/custom/overrides",
            "build",
            "project1",
        ]
    )

    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/custom/patches1"), pathlib.Path("/custom/patches2")],
        [pathlib.Path("/custom/overrides")],
        commit=False,
    )


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_skip_if_already_prepared(mock_preparer, monkeypatch):
    """Test that preparation is skipped if ZMAKE_WORKSPACE_PREPARED is set."""
    monkeypatch.setenv("ZMAKE_WORKSPACE_PREPARED", "1")
    main.main(["build", "project1"])
    mock_preparer.assert_not_called()


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_compare_builds_triggers_prep(mock_preparer):
    """Test that 'zmake compare-builds' triggers codebase preparation."""
    with mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        # We need to mock tempfile.mkdtemp because compare-builds calls it
        with mock.patch("tempfile.mkdtemp", return_value="/fake/tempdir"):
            main.main(["compare-builds", "ref1", "ref2", "project1"])

    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        commit=False,
    )


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
@mock.patch("zmake.__main__.copy_outputs_back")
def test_main_build_shadow_prep(
    mock_copy, mock_create, _mock_has_files, mock_preparer
):
    """Test that shadow prep is used when overrides/patches exist."""
    mock_ec_module = mock.Mock()
    mock_ec_module.resolve.return_value = pathlib.Path(
        "/fake/checkout/src/platform/ec"
    )
    modules_mock = {"ec": mock_ec_module}

    with mock.patch(
        "zmake.__main__.modules.locate_from_checkout",
        return_value=modules_mock,
    ), mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        main.main(["build", "project1"])

    # Verify shadow checkout was created
    mock_create.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        mock.ANY,
        modules_mock,
        pathlib.Path("/fake/checkout/src/third_party/zephyrproject/zephyr"),
        [pathlib.Path("/fake/overrides")],
        [pathlib.Path("/fake/patches")],
    )
    shadow_path = mock_create.call_args[0][1]
    assert shadow_path.name == "shadow"
    assert isinstance(shadow_path, pathlib.Path)

    # Verify preparer was called with shadow path and commit=True
    mock_preparer.assert_called_once_with(
        shadow_path,
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        commit=True,
    )

    # Verify outputs copied back
    mock_copy.assert_called_once_with(
        shadow_path,
        pathlib.Path("/fake/checkout"),
        pathlib.Path("src/platform/ec"),
    )


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
@mock.patch("zmake.__main__.copy_outputs_back")
def test_main_build_no_shadow_forced(
    mock_copy, mock_create, _mock_has_files, mock_preparer
):
    """Test that --no-shadow forces in-place preparation even if files exist."""
    with mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        main.main(["--no-shadow", "build", "project1"])

    mock_create.assert_not_called()
    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        commit=False,
    )
    mock_copy.assert_not_called()
