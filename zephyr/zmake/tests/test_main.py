# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake main entry point and integration with codebase prep."""

import pathlib
from unittest import mock

import pytest
import zmake.__main__ as main


@pytest.fixture(name="mock_zmake_class")
def fixture_mock_zmake_class():
    """Mock the Zmake class."""
    with mock.patch("zmake.zmake.Zmake", autospec=True) as mocked:
        instance = mocked.return_value
        instance.executor = mock.Mock()
        instance.executor.wait.return_value = 0
        instance.cmp_failed_projects = {}
        instance.failed_projects = []
        yield mocked


@pytest.fixture(name="mock_preparer")
def fixture_mock_preparer():
    """Mock the CodebasePreparer class."""
    with mock.patch(
        "zmake.repository_prep.CodebasePreparer", autospec=True
    ) as mocked:
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
    """Ensure ZMAKE_WORKSPACE_PREPARED and MAKEFLAGS are not set."""
    monkeypatch.delenv("ZMAKE_WORKSPACE_PREPARED", raising=False)
    monkeypatch.delenv("MAKEFLAGS", raising=False)
    monkeypatch.setattr(main, "maybe_reexec", lambda argv: None)
    monkeypatch.setattr(main, "find_toolchains", lambda: None)


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
def test_main_build_triggers_prep(mock_preparer):
    """Test that 'zmake build' triggers codebase preparation."""
    with mock.patch(
        "zmake.repository_prep.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.repository_prep.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        main.main(["build", "project1"])

    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
    )
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
        "zmake.repository_prep.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.repository_prep.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        with mock.patch("tempfile.mkdtemp", return_value="/fake/tempdir"):
            main.main(["compare-builds", "ref1", "ref2", "project1"])

    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
    )


@pytest.mark.usefixtures("clean_env")
@mock.patch("zmake.repository_prep._has_files", return_value=True)
@mock.patch("zmake.repository_prep.create_shadow_checkout")
@mock.patch("zmake.repository_prep.copy_outputs_back")
def test_main_build_shadow_and_remaps_cli_opts(
    mock_copy, mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test shadow prep is used by default and remaps CLI paths into shadow."""
    mock_ec_module = mock.Mock()
    mock_ec_module.resolve.return_value = pathlib.Path(
        "/fake/checkout/src/platform/ec"
    )
    zp_rel = pathlib.Path("src/third_party/zephyrproject/zephyr")

    with mock.patch(
        "zmake.modules.locate_from_checkout",
        return_value={"ec": mock_ec_module},
    ), mock.patch(
        "zmake.repository_prep.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.repository_prep.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ), mock.patch(
        "zmake.repository_prep.find_target_repos",
        return_value=set(),
    ):
        main.main(
            [
                "--checkout",
                "/fake/checkout",
                "--zephyr-base",
                "/external/zephyr",
                "--projects-dir",
                "/fake/checkout/src/platform/ec/zephyr/program",
                "build",
                "project1",
            ]
        )

    shadow_path = mock_create.call_args[0][1]
    mock_preparer.assert_called_once_with(
        shadow_path,
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        set(),
        commit=True,
    )
    _, zmake_kwargs = mock_zmake_class.call_args
    assert zmake_kwargs["checkout"] == shadow_path
    assert zmake_kwargs["zephyr_base"] == shadow_path / zp_rel
    assert zmake_kwargs["projects_dirs"] == [
        shadow_path / "src" / "platform" / "ec" / "zephyr" / "program"
    ]
    mock_copy.assert_called_once_with(
        shadow_path,
        pathlib.Path("/fake/checkout"),
        pathlib.Path("src/platform/ec"),
    )


@pytest.mark.usefixtures(
    "mock_zmake_class", "mock_locate_checkout", "clean_env"
)
@mock.patch("zmake.repository_prep._has_files", return_value=True)
@mock.patch("zmake.repository_prep.create_shadow_checkout")
def test_main_build_no_shadow_forced(
    mock_create, _mock_has_files, mock_preparer
):
    """Test that --no-shadow forces in-place preparation even if files exist."""
    with mock.patch(
        "zmake.repository_prep.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.repository_prep.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ):
        main.main(["--no-shadow", "build", "project1"])

    mock_create.assert_not_called()
    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
    )


@pytest.mark.usefixtures("mock_zmake_class", "clean_env")
def test_main_modules_dir_workspace_prepared_markers(
    mock_preparer, mock_locate_checkout, tmp_path
):
    """Test --modules-dir skips checkout lookup and detects .workspace_prepared."""
    unprepared = tmp_path / "unprepared" / "modules"
    (unprepared / "ec").mkdir(parents=True)
    main.main(
        [
            "--modules-dir",
            str(unprepared),
            "--zephyr-base",
            str(tmp_path / "unprepared" / "zephyr"),
            "build",
            "project1",
        ]
    )
    mock_locate_checkout.assert_not_called()

    for marker_rel in (
        "work/.workspace_prepared",
        "work/modules/ec/.workspace_prepared",
    ):
        base = tmp_path / marker_rel.replace("/", "_")
        mod_dir = base / "work" / "modules"
        (mod_dir / "ec").mkdir(parents=True)
        (base / marker_rel).touch()
        main.main(
            [
                "--checkout",
                str(base / "work"),
                "--modules-dir",
                str(mod_dir),
                "--zephyr-base",
                str(base / "work" / "zephyr"),
                "build",
                "project1",
            ]
        )
    mock_preparer.assert_not_called()


@pytest.mark.usefixtures("mock_locate_checkout", "clean_env")
@mock.patch("zmake.repository_prep._has_files", return_value=True)
@mock.patch("zmake.repository_prep.create_shadow_checkout")
def test_main_compare_builds_defers_prep_to_per_ref_worktrees(
    mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test compare-builds forwards overrides/patches to per-ref worktrees."""
    with mock.patch(
        "zmake.repository_prep.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.repository_prep.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ), mock.patch(
        "tempfile.mkdtemp", return_value="/fake/tempdir"
    ):
        main.main(
            [
                "compare-builds",
                "--ref1",
                "HEAD",
                "--ref2",
                "HEAD~",
                "project1",
            ]
        )

    mock_create.assert_not_called()
    mock_preparer.assert_not_called()
    zmake_instance = mock_zmake_class.return_value
    _, cmp_kwargs = zmake_instance.compare_builds.call_args
    assert cmp_kwargs["worktree_patches_dirs"] == [
        pathlib.Path("/fake/patches")
    ]
    assert cmp_kwargs["worktree_src_override_dirs"] == [
        pathlib.Path("/fake/overrides")
    ]


@pytest.mark.usefixtures("mock_locate_checkout", "clean_env")
@mock.patch("zmake.repository_prep._has_files", return_value=True)
@mock.patch("zmake.repository_prep.create_shadow_checkout")
def test_main_compare_builds_no_shadow_uses_in_place_prep(
    mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test compare-builds with --no-shadow uses in-place preparation."""
    with mock.patch("tempfile.mkdtemp", return_value="/fake/tempdir"):
        main.main(
            [
                "--no-shadow",
                "--patches-dir",
                "/fake/patches",
                "--src-override-dir",
                "/fake/overrides",
                "compare-builds",
                "--ref1",
                "HEAD",
                "--ref2",
                "HEAD~",
                "project1",
            ]
        )

    mock_create.assert_not_called()
    mock_preparer.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
    )
    zmake_instance = mock_zmake_class.return_value
    _, cmp_kwargs = zmake_instance.compare_builds.call_args
    assert cmp_kwargs.get("worktree_patches_dirs") is None
    assert cmp_kwargs.get("worktree_src_override_dirs") is None
