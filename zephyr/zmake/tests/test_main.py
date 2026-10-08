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
    """Ensure ZMAKE_WORKSPACE_PREPARED and MAKEFLAGS are not set."""
    monkeypatch.delenv("ZMAKE_WORKSPACE_PREPARED", raising=False)
    monkeypatch.delenv("MAKEFLAGS", raising=False)
    monkeypatch.setattr(main, "maybe_reexec", lambda argv: None)
    monkeypatch.setattr(main, "find_toolchains", lambda: None)


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
    dirty_set = {pathlib.Path("src/platform/ec/dirty.c")}

    with mock.patch(
        "zmake.__main__.modules.locate_from_checkout",
        return_value=modules_mock,
    ), mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ), mock.patch(
        "zmake.__main__.find_target_repos",
        return_value={pathlib.Path("/fake/checkout/src/platform/ec")},
    ), mock.patch(
        "zmake.__main__.get_dirty_files",
        return_value=dirty_set,
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

    # Verify preparer was called with shadow path, user_dirty_files, and commit=True
    mock_preparer.assert_called_once_with(
        shadow_path,
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        user_dirty_files=dirty_set,
        commit=True,
    )

    # Verify outputs copied back
    mock_copy.assert_called_once_with(
        shadow_path,
        pathlib.Path("/fake/checkout"),
        pathlib.Path("src/platform/ec"),
    )


@pytest.mark.usefixtures("mock_locate_checkout", "clean_env")
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
def test_main_compare_builds_defers_prep_to_per_ref_worktrees(
    mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test compare-builds forwards overrides/patches to per-ref worktrees."""
    dirty_set = {pathlib.Path("src/platform/ec/dirty.c")}
    with mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ), mock.patch(
        "zmake.__main__.find_target_repos",
        return_value={pathlib.Path("/fake/checkout/src/platform/ec")},
    ), mock.patch(
        "zmake.__main__.get_dirty_files",
        return_value=dirty_set,
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
    assert cmp_kwargs.get("user_dirty_files") is None


@pytest.mark.usefixtures("mock_locate_checkout", "clean_env")
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
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
        commit=False,
    )
    zmake_instance = mock_zmake_class.return_value
    _, cmp_kwargs = zmake_instance.compare_builds.call_args
    assert cmp_kwargs.get("worktree_patches_dirs") is None
    assert cmp_kwargs.get("worktree_src_override_dirs") is None


@pytest.mark.usefixtures("clean_env")
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
@mock.patch("zmake.__main__.copy_outputs_back")
def test_main_checkout_uses_shadow_and_remaps_zephyr_base(
    _mock_copy, mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test --checkout uses shadow mode and remaps --zephyr-base/--projects-dir."""
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
    ), mock.patch(
        "zmake.__main__.find_target_repos",
        return_value=set(),
    ):
        main.main(
            [
                "--checkout",
                "/fake/checkout",
                "--zephyr-base",
                "/fake/checkout/src/third_party/zephyrproject/zephyr",
                "--projects-dir",
                "/fake/checkout/src/platform/ec/zephyr/program",
                "build",
                "project1",
            ]
        )

    mock_create.assert_called_once()
    shadow_path = mock_create.call_args[0][1]
    mock_preparer.assert_called_once_with(
        shadow_path,
        [pathlib.Path("/fake/patches")],
        [pathlib.Path("/fake/overrides")],
        user_dirty_files=set(),
        commit=True,
    )
    _, zmake_kwargs = mock_zmake_class.call_args
    assert zmake_kwargs["checkout"] == shadow_path
    assert zmake_kwargs["zephyr_base"] == (
        shadow_path / "src" / "third_party" / "zephyrproject" / "zephyr"
    )
    assert zmake_kwargs["projects_dirs"] == [
        shadow_path / "src" / "platform" / "ec" / "zephyr" / "program"
    ]


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


@pytest.mark.usefixtures("mock_zmake_class", "clean_env")
def test_main_build_modules_dir_prep(mock_preparer):
    """Test that passing --modules-dir and --zephyr-base prepares the ebuild worktree."""
    ec_mod = pathlib.Path("/fake/work/zephyrproject/modules/ec")
    with mock.patch(
        "zmake.__main__.modules.locate_from_directory",
        return_value={"ec": ec_mod},
    ):
        main.main(
            [
                "--modules-dir",
                "/fake/work/zephyrproject/modules",
                "--zephyr-base",
                "/fake/work/zephyrproject/zephyr",
                "build",
                "project1",
            ]
        )

    mock_preparer.assert_called_once()
    call_args, call_kwargs = mock_preparer.call_args
    assert call_args[0] == pathlib.Path("/fake/work/zephyrproject")
    assert call_kwargs["commit"] is False
    assert call_kwargs["allow_dirty"] is True
    assert call_kwargs["path_mappings"][pathlib.Path("src/platform/ec")] == (
        ec_mod
    )


@pytest.mark.usefixtures("mock_zmake_class", "clean_env")
def test_main_build_modules_dir_zephyr_ec_private(mock_preparer, tmp_path):
    """Test --modules-dir locates sysroot zephyr-ec-private when present."""
    sysroot = tmp_path / "sysroot"
    zephyr_priv = (
        sysroot / "firmware" / "private-directories" / "zephyr-ec-private"
    )
    zephyr_priv.mkdir(parents=True)
    ec_mod = tmp_path / "work" / "zephyrproject" / "modules" / "ec"
    ec_mod.mkdir(parents=True)
    with mock.patch.dict("os.environ", {"SYSROOT": str(sysroot)}), mock.patch(
        "zmake.__main__.modules.locate_from_directory",
        return_value={"ec": ec_mod},
    ):
        main.main(
            [
                "--modules-dir",
                str(ec_mod.parent),
                "--zephyr-base",
                str(ec_mod.parent.parent / "zephyr"),
                "build",
                "project1",
            ]
        )

    call_args, _ = mock_preparer.call_args
    assert zephyr_priv / "patches" in call_args[1]
    assert zephyr_priv / "src-override" in call_args[2]


@pytest.mark.usefixtures("clean_env")
@mock.patch("zmake.__main__._has_files", return_value=True)
@mock.patch("zmake.__main__.create_shadow_checkout")
@mock.patch("zmake.__main__.copy_outputs_back")
def test_main_external_zephyr_base_remapped_to_shadow(
    _mock_copy, mock_create, _mock_has_files, mock_preparer, mock_zmake_class
):
    """Test external --zephyr-base is remapped into shadow checkout and checked for dirty files."""
    mock_ec_module = mock.Mock()
    mock_ec_module.resolve.return_value = pathlib.Path(
        "/fake/checkout/src/platform/ec"
    )
    zp_rel = pathlib.Path("src/third_party/zephyrproject/zephyr")

    with mock.patch(
        "zmake.__main__.modules.locate_from_checkout",
        return_value={"ec": mock_ec_module},
    ), mock.patch(
        "zmake.__main__.get_default_patches_dirs",
        return_value=[pathlib.Path("/fake/patches")],
    ), mock.patch(
        "zmake.__main__.get_default_src_override_dirs",
        return_value=[pathlib.Path("/fake/overrides")],
    ), mock.patch(
        "zmake.__main__.find_target_repos",
        return_value={pathlib.Path("/external/zephyr")},
    ) as mock_find_repos, mock.patch(
        "zmake.__main__.get_dirty_files",
        return_value=set(),
    ) as mock_get_dirty:
        main.main(
            [
                "--checkout",
                "/fake/checkout",
                "--zephyr-base",
                "/external/zephyr",
                "build",
                "project1",
            ]
        )

    ext_map = {zp_rel: pathlib.Path("/external/zephyr")}
    mock_find_repos.assert_called_once_with(
        pathlib.Path("/fake/checkout"),
        [pathlib.Path("/fake/overrides")],
        [pathlib.Path("/fake/patches")],
        ext_map,
    )
    mock_get_dirty.assert_called_once_with(
        pathlib.Path("/external/zephyr"),
        pathlib.Path("/fake/checkout"),
        ext_map,
    )
    shadow_path = mock_create.call_args[0][1]
    mock_preparer.assert_called_once()
    _, zmake_kwargs = mock_zmake_class.call_args
    assert zmake_kwargs["zephyr_base"] == shadow_path / zp_rel


@pytest.mark.usefixtures("mock_zmake_class", "clean_env")
def test_main_modules_dir_workspace_prepared_markers(mock_preparer, tmp_path):
    """Test --modules-dir detects .workspace_prepared in parent and modules/ec."""
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
                "--modules-dir",
                str(mod_dir),
                "--zephyr-base",
                str(base / "work" / "zephyr"),
                "build",
                "project1",
            ]
        )
    mock_preparer.assert_not_called()
