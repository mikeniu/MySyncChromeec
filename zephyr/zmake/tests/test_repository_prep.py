# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for codebase preparation (patches and overrides)."""

import pathlib
import subprocess

import pytest
from zmake.repository_prep import _find_override_files
from zmake.repository_prep import _find_patch_files
from zmake.repository_prep import CodebasePreparer
from zmake.repository_prep import get_dirty_files
from zmake.repository_prep import RepositoryPrepError


def test_find_override_files(tmp_path):
    """Test that override files are correctly filtered."""
    (tmp_path / "file1.c").touch()
    (tmp_path / "subdir").mkdir()
    (tmp_path / "subdir" / "file2.h").touch()

    (tmp_path / ".gitkeep").touch()
    (tmp_path / ".keep").touch()
    (tmp_path / "README.md").touch()
    (tmp_path / "OWNERS").touch()
    (tmp_path / "DIR_METADATA").touch()
    (tmp_path / "LICENSE").touch()
    (tmp_path / "subdir" / "README.md").touch()

    files = list(_find_override_files(tmp_path))
    rel_files = sorted([f.relative_to(tmp_path) for f in files])

    assert rel_files == [
        pathlib.Path("file1.c"),
        pathlib.Path("subdir/file2.h"),
    ]


def test_find_patch_files(tmp_path):
    """Test that patch files are correctly found."""
    (tmp_path / "patch1.patch").touch()
    (tmp_path / "subdir").mkdir()
    (tmp_path / "subdir" / "patch2.patch").touch()
    (tmp_path / "not_a_patch.txt").touch()

    files = list(_find_patch_files(tmp_path))
    rel_files = sorted([f.relative_to(tmp_path) for f in files])

    assert rel_files == [
        pathlib.Path("patch1.patch"),
        pathlib.Path("subdir/patch2.patch"),
    ]


def test_codebase_preparer_overrides(tmp_path, setup_git_repo):
    """Test applying and reverting source overrides."""
    ec_path = tmp_path / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)

    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "file_to_override.c").write_text(
        "overridden content\n", encoding="utf-8"
    )
    (override_ec / "new_file.c").write_text(
        "new file content\n", encoding="utf-8"
    )

    preparer = CodebasePreparer(
        tmp_path,
        patches_dirs=[],
        src_override_dirs=[override_root],
    )

    preparer.prepare()

    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "overridden content\n"
    assert (ec_path / "new_file.c").read_text(
        encoding="utf-8"
    ) == "new file content\n"

    preparer.restore()

    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "original content\n"
    assert not (ec_path / "new_file.c").exists()


def test_codebase_preparer_dirty_check(tmp_path, setup_git_repo):
    """Test that overrides fail if target file is dirty."""
    ec_path = tmp_path / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)

    (ec_path / "file_to_override.c").write_text(
        "dirty content\n", encoding="utf-8"
    )

    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "file_to_override.c").write_text(
        "overridden content\n", encoding="utf-8"
    )

    preparer = CodebasePreparer(
        tmp_path,
        patches_dirs=[],
        src_override_dirs=[override_root],
    )

    with pytest.raises(RepositoryPrepError):
        preparer.prepare()


def test_codebase_preparer_patches(tmp_path, setup_git_repo):
    """Test applying and reverting patches."""
    ec_path = tmp_path / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)

    patch_dir = tmp_path / "patches"
    patch_ec = patch_dir / "src" / "platform" / "ec"
    patch_ec.mkdir(parents=True)

    (ec_path / "file_to_patch.c").write_text(
        "original patch content\nmodified patch content\n", encoding="utf-8"
    )
    diff_result = subprocess.run(
        ["git", "diff"],
        cwd=ec_path,
        capture_output=True,
        text=True,
        check=True,
    )
    (patch_ec / "0001-test.patch").write_text(
        diff_result.stdout, encoding="utf-8"
    )

    subprocess.run(["git", "restore", "."], cwd=ec_path, check=True)
    assert (ec_path / "file_to_patch.c").read_text(
        encoding="utf-8"
    ) == "original patch content\n"

    preparer = CodebasePreparer(
        tmp_path,
        patches_dirs=[patch_dir],
        src_override_dirs=[],
    )

    preparer.prepare()

    assert (ec_path / "file_to_patch.c").read_text(encoding="utf-8") == (
        "original patch content\nmodified patch content\n"
    )

    preparer.restore()

    assert (ec_path / "file_to_patch.c").read_text(
        encoding="utf-8"
    ) == "original patch content\n"


def test_codebase_preparer_override_then_patch(tmp_path, setup_git_repo):
    """Test overriding a file and then patching it in-place without false dirty error."""
    ec_path = tmp_path / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)

    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "file_to_override.c").write_text(
        "overridden line 1\n", encoding="utf-8"
    )

    patch_dir = tmp_path / "patches"
    patch_ec = patch_dir / "src" / "platform" / "ec"
    patch_ec.mkdir(parents=True)
    (patch_ec / "0001-patch-override.patch").write_text(
        "--- a/file_to_override.c\n"
        "+++ b/file_to_override.c\n"
        "@@ -1 +1,2 @@\n"
        " overridden line 1\n"
        "+patched line 2\n",
        encoding="utf-8",
    )

    with CodebasePreparer(
        tmp_path,
        patches_dirs=[patch_dir],
        src_override_dirs=[override_root],
    ):
        assert (ec_path / "file_to_override.c").read_text(encoding="utf-8") == (
            "overridden line 1\npatched line 2\n"
        )

    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "original content\n"


def test_get_dirty_files_untracked_subdir_and_rename(tmp_path, setup_git_repo):
    """Test get_dirty_files reports files in untracked subdirs and renames."""
    ec_path = tmp_path / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)

    new_subdir = ec_path / "new_dir" / "nested"
    new_subdir.mkdir(parents=True)
    (new_subdir / "untracked.c").write_text("untracked\n", encoding="utf-8")
    subprocess.run(
        ["git", "mv", "file_to_override.c", "renamed.c"],
        cwd=ec_path,
        check=True,
    )

    dirty = get_dirty_files(ec_path, tmp_path)
    assert pathlib.Path("src/platform/ec/new_dir/nested/untracked.c") in dirty
    assert pathlib.Path("src/platform/ec/file_to_override.c") in dirty
    assert pathlib.Path("src/platform/ec/renamed.c") in dirty
