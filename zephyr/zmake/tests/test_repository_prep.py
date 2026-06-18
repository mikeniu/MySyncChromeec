# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for codebase preparation (patches and overrides)."""

import pathlib
import subprocess
import tempfile

import pytest
from zmake.repository_prep import _find_override_files
from zmake.repository_prep import _find_patch_files
from zmake.repository_prep import _has_files
from zmake.repository_prep import CodebasePreparer
from zmake.repository_prep import copy_outputs_back
from zmake.repository_prep import create_shadow_checkout
from zmake.repository_prep import main as prep_main


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


def test_codebase_preparer_commit():
    """Test that commit=True commits changes and skips restore."""
    with tempfile.TemporaryDirectory() as checkout_dir:
        checkout_path = pathlib.Path(checkout_dir)
        ec_path = checkout_path / "src" / "platform" / "ec"
        ec_path.mkdir(parents=True)
        setup_mock_git_repo(ec_path)

        # Create overrides
        override_root = checkout_path / "overrides"
        override_ec = override_root / "src" / "platform" / "ec"
        override_ec.mkdir(parents=True)
        (override_ec / "file_to_override.c").write_text("overridden content\n")

        # Prep preparer with commit=True
        preparer = CodebasePreparer(
            checkout_path,
            patches_dirs=[],
            src_override_dirs=[override_root],
            commit=True,
        )

        # Apply
        preparer.prepare()

        assert (
            ec_path / "file_to_override.c"
        ).read_text() == "overridden content\n"

        # Verify it was committed (git status should be clean)
        status = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=ec_path,
            capture_output=True,
            text=True,
            check=True,
        )
        assert status.stdout.strip() == ""

        # Verify commit message
        log = subprocess.run(
            ["git", "log", "-n", "1", "--format=%s"],
            cwd=ec_path,
            capture_output=True,
            text=True,
            check=True,
        )
        assert log.stdout.strip() == "applied patches and overrides"

        # Revert should be a no-op when commit=True
        preparer.restore()

        # Content should still be overridden because restore was skipped
        assert (
            ec_path / "file_to_override.c"
        ).read_text() == "overridden content\n"


def test_has_files_ignores_metadata():
    """Test that _has_files ignores .gitkeep, .keep, README.md, OWNERS, etc."""
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = pathlib.Path(temp_dir)
        assert not _has_files(temp_path)

        (temp_path / ".gitkeep").touch()
        (temp_path / ".keep").touch()
        (temp_path / "README.md").touch()
        (temp_path / "OWNERS").touch()
        (temp_path / "DIR_METADATA").touch()
        (temp_path / "LICENSE").touch()
        assert not _has_files(temp_path)

        (temp_path / "real_patch.patch").touch()
        assert _has_files(temp_path)


def test_codebase_preparer_override_then_patch_and_restore():
    """Test overriding a file, patching that overridden file, and restoring both."""
    with tempfile.TemporaryDirectory() as checkout_dir:
        checkout_path = pathlib.Path(checkout_dir)
        ec_path = checkout_path / "src" / "platform" / "ec"
        ec_path.mkdir(parents=True)
        setup_mock_git_repo(ec_path)

        override_root = checkout_path / "overrides"
        override_ec = override_root / "src" / "platform" / "ec"
        override_ec.mkdir(parents=True)
        (override_ec / "file_to_override.c").write_text("overridden line 1\n")

        patch_dir = checkout_path / "patches"
        patch_ec = patch_dir / "src" / "platform" / "ec"
        patch_ec.mkdir(parents=True)
        (patch_ec / "0001-patch-overridden.patch").write_text(
            "--- a/file_to_override.c\n"
            "+++ b/file_to_override.c\n"
            "@@ -1 +1,2 @@\n"
            " overridden line 1\n"
            "+patched line 2\n"
        )

        preparer = CodebasePreparer(
            checkout_path,
            patches_dirs=[patch_dir],
            src_override_dirs=[override_root],
        )
        preparer.prepare()
        assert (ec_path / "file_to_override.c").read_text() == (
            "overridden line 1\npatched line 2\n"
        )

        preparer.restore()
        assert (
            ec_path / "file_to_override.c"
        ).read_text() == "original content\n"


def test_codebase_preparer_path_mappings_and_cli():
    """Test path_mappings in non-git ebuild layout and standalone CLI main()."""
    with tempfile.TemporaryDirectory() as work_dir:
        work_path = pathlib.Path(work_dir)
        ec_mod = work_path / "zephyrproject" / "modules" / "ec"
        ec_mod.mkdir(parents=True)
        (ec_mod / "foo.c").write_text("base\n")

        override_root = work_path / "overrides"
        (override_root / "src" / "platform" / "ec").mkdir(parents=True)
        (override_root / "src" / "platform" / "ec" / "foo.c").write_text(
            "mapped override\n"
        )

        preparer = CodebasePreparer(
            work_path,
            patches_dirs=[],
            src_override_dirs=[override_root],
            path_mappings={"src/platform/ec": ec_mod},
            allow_dirty=True,
        )
        preparer.prepare()
        assert (ec_mod / "foo.c").read_text() == "mapped override\n"

        # Test standalone CLI with explicit checkout and override dir
        checkout_dir = work_path / "direct_checkout"
        (checkout_dir / "src" / "platform" / "ec").mkdir(parents=True)
        ret = prep_main(
            [
                "--checkout",
                str(checkout_dir),
                "--src-override-dir",
                str(override_root),
                "--allow-dirty",
            ]
        )
        assert ret == 0
        assert (
            checkout_dir / "src" / "platform" / "ec" / "foo.c"
        ).read_text() == "mapped override\n"


def test_copy_outputs_back_merges_and_fixes_lcov_paths():
    """Test that copy_outputs_back preserves prior build subdirs and fixes lcov paths."""
    with tempfile.TemporaryDirectory() as work_dir:
        work_path = pathlib.Path(work_dir)
        real_co = work_path / "real"
        shadow_co = work_path / "shadow"
        ec_rel = pathlib.Path("src/platform/ec")

        real_build = real_co / ec_rel / "build"
        (real_build / "board_a").mkdir(parents=True)
        (real_build / "board_a" / "ec.bin").write_text("bin_a\n")

        shadow_build = shadow_co / ec_rel / "build"
        (shadow_build / "board_b").mkdir(parents=True)
        (shadow_build / "board_b" / "ec.bin").write_text("bin_b\n")
        (shadow_build / "lcov.info").write_text(
            f"SF:{shadow_co}/src/platform/ec/main.c\nend_of_record\n"
        )

        copy_outputs_back(shadow_co, real_co, ec_rel)

        assert (real_build / "board_a" / "ec.bin").read_text() == "bin_a\n"
        assert (real_build / "board_b" / "ec.bin").read_text() == "bin_b\n"
        assert (real_build / "lcov.info").read_text() == (
            f"SF:{real_co}/src/platform/ec/main.c\nend_of_record\n"
        )


def test_create_shadow_checkout_deduplicates_and_expands_fingerprint():
    """Test create_shadow_checkout handles overlapping module and fingerprint paths."""
    with tempfile.TemporaryDirectory() as work_dir:
        work_path = pathlib.Path(work_dir)
        real_co = work_path / "real"
        shadow_co = work_path / "shadow"

        ec_dir = real_co / "src" / "platform" / "ec"
        ec_dir.mkdir(parents=True)
        (ec_dir / "main.c").write_text("int main() {}\n")
        (ec_dir / "twister-out-host.2").mkdir()
        (ec_dir / "twister-out-host.2" / "log.txt").write_text("ignored\n")

        boringssl = real_co / "src" / "third_party" / "boringssl"
        boringssl.mkdir(parents=True)

        egis_fp = real_co / "src" / "platform" / "fingerprint" / "egis"
        egis_fp.mkdir(parents=True)
        fpalg = real_co / "src" / "platform" / "fingerprint" / "google-fpalg"
        fpalg.mkdir(parents=True)

        zephyr_base = (
            real_co / "src" / "third_party" / "zephyrproject" / "zephyr"
        )
        zephyr_base.mkdir(parents=True)

        modules = {
            "ec": ec_dir,
            "boringssl": boringssl,
            "egis_fp": egis_fp,
        }

        create_shadow_checkout(real_co, shadow_co, modules, zephyr_base, [], [])

        shadow_ec = shadow_co / "src" / "platform" / "ec"
        assert shadow_ec.is_dir() and not shadow_ec.is_symlink()
        assert not (shadow_ec / "twister-out-host.2").exists()
        short_hash = subprocess.run(
            ["git", "rev-parse", "--short", "--verify", "HEAD"],
            cwd=shadow_ec,
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
        assert len(short_hash) >= 8
        assert (shadow_co / "src" / "third_party" / "boringssl").is_symlink()
        assert (
            shadow_co / "src" / "platform" / "fingerprint" / "egis"
        ).is_symlink()
        assert (
            shadow_co / "src" / "platform" / "fingerprint" / "google-fpalg"
        ).is_symlink()
