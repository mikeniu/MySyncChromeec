# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for codebase preparation (patches and overrides)."""

import pathlib
import subprocess
import tempfile

import pytest  # pylint: disable=import-error
from zmake.repository_prep import _find_override_files
from zmake.repository_prep import _find_patch_files
from zmake.repository_prep import CodebasePreparer


def test_find_override_files():
    """Test that override files are correctly filtered."""
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = pathlib.Path(temp_dir)
        # Create some files
        (temp_path / "file1.c").touch()
        (temp_path / "subdir").mkdir()
        (temp_path / "subdir" / "file2.h").touch()

        # Create ignored files
        (temp_path / ".gitkeep").touch()
        (temp_path / "README.md").touch()
        (temp_path / "OWNERS").touch()
        (temp_path / "DIR_METADATA").touch()
        (temp_path / "LICENSE").touch()
        (temp_path / "subdir" / "README.md").touch()

        files = list(_find_override_files(temp_path))
        rel_files = sorted([f.relative_to(temp_path) for f in files])

        assert rel_files == [
            pathlib.Path("file1.c"),
            pathlib.Path("subdir/file2.h"),
        ]


def test_find_patch_files():
    """Test that patch files are correctly found."""
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = pathlib.Path(temp_dir)
        (temp_path / "patch1.patch").touch()
        (temp_path / "subdir").mkdir()
        (temp_path / "subdir" / "patch2.patch").touch()
        (temp_path / "not_a_patch.txt").touch()

        files = list(_find_patch_files(temp_path))
        rel_files = sorted([f.relative_to(temp_path) for f in files])

        assert rel_files == [
            pathlib.Path("patch1.patch"),
            pathlib.Path("subdir/patch2.patch"),
        ]


def setup_mock_git_repo(repo_dir):
    """Initialize a git repo and make an initial commit."""
    repo_path = pathlib.Path(repo_dir)
    subprocess.run(
        ["git", "init", "--initial-branch=main"], cwd=repo_path, check=True
    )
    subprocess.run(
        ["git", "config", "user.name", "Test User"], cwd=repo_path, check=True
    )
    subprocess.run(
        ["git", "config", "user.email", "test@user.com"],
        cwd=repo_path,
        check=True,
    )

    (repo_path / "file_to_override.c").write_text("original content\n")
    (repo_path / "file_to_patch.c").write_text("original patch content\n")

    subprocess.run(["git", "add", "."], cwd=repo_path, check=True)
    subprocess.run(
        ["git", "commit", "-m", "initial commit"], cwd=repo_path, check=True
    )


def test_codebase_preparer_overrides():
    """Test applying and reverting source overrides."""
    with tempfile.TemporaryDirectory() as checkout_dir:
        checkout_path = pathlib.Path(checkout_dir)
        # We need to simulate the structure.
        # Let's say ec repo is at checkout_path / "src" / "platform" / "ec"
        ec_path = checkout_path / "src" / "platform" / "ec"
        ec_path.mkdir(parents=True)
        setup_mock_git_repo(ec_path)

        # Create overrides
        override_root = checkout_path / "overrides"
        override_ec = override_root / "src" / "platform" / "ec"
        override_ec.mkdir(parents=True)
        (override_ec / "file_to_override.c").write_text("overridden content\n")
        (override_ec / "new_file.c").write_text("new file content\n")

        # Prep preparer
        # We pass absolute path of override_root
        preparer = CodebasePreparer(
            checkout_path,
            patches_dirs=[],
            src_override_dirs=[override_root],
        )

        # Apply
        preparer.prepare()

        assert (
            ec_path / "file_to_override.c"
        ).read_text() == "overridden content\n"
        assert (ec_path / "new_file.c").read_text() == "new file content\n"

        # Revert
        preparer.restore()

        assert (
            ec_path / "file_to_override.c"
        ).read_text() == "original content\n"
        assert not (ec_path / "new_file.c").exists()


def test_codebase_preparer_dirty_check():
    """Test that overrides fail if target file is dirty."""
    with tempfile.TemporaryDirectory() as checkout_dir:
        checkout_path = pathlib.Path(checkout_dir)
        ec_path = checkout_path / "src" / "platform" / "ec"
        ec_path.mkdir(parents=True)
        setup_mock_git_repo(ec_path)

        # Make file dirty in "user workspace"
        (ec_path / "file_to_override.c").write_text("dirty content\n")

        # Create overrides
        override_root = checkout_path / "overrides"
        override_ec = override_root / "src" / "platform" / "ec"
        override_ec.mkdir(parents=True)
        (override_ec / "file_to_override.c").write_text("overridden content\n")

        # Prep preparer
        preparer = CodebasePreparer(
            checkout_path,
            patches_dirs=[],
            src_override_dirs=[override_root],
        )

        # Apply should exit with system exit because of dirty file
        with pytest.raises(SystemExit):
            preparer.prepare()


def test_codebase_preparer_patches():
    """Test applying and reverting patches."""
    with tempfile.TemporaryDirectory() as checkout_dir:
        checkout_path = pathlib.Path(checkout_dir)
        ec_path = checkout_path / "src" / "platform" / "ec"
        ec_path.mkdir(parents=True)
        setup_mock_git_repo(ec_path)

        # Create a patch
        # We need a patch that modifies file_to_patch.c
        # Let's generate it using git
        patch_dir = checkout_path / "patches"
        patch_ec = patch_dir / "src" / "platform" / "ec"
        patch_ec.mkdir(parents=True)

        # Modify file and diff to get patch
        (ec_path / "file_to_patch.c").write_text(
            "original patch content\nmodified patch content\n"
        )
        diff_result = subprocess.run(
            ["git", "diff"],
            cwd=ec_path,
            capture_output=True,
            text=True,
            check=True,
        )
        (patch_ec / "0001-test.patch").write_text(diff_result.stdout)

        # Revert the local change so we can test applying the patch
        subprocess.run(["git", "restore", "."], cwd=ec_path, check=True)
        assert (
            ec_path / "file_to_patch.c"
        ).read_text() == "original patch content\n"

        # Prep preparer
        preparer = CodebasePreparer(
            checkout_path,
            patches_dirs=[patch_dir],
            src_override_dirs=[],
        )

        # Apply
        preparer.prepare()

        assert (ec_path / "file_to_patch.c").read_text() == (
            "original patch content\nmodified patch content\n"
        )

        # Revert
        preparer.restore()

        assert (
            ec_path / "file_to_patch.c"
        ).read_text() == "original patch content\n"
