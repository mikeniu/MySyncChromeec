#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for codebase preparation (patches and overrides)."""

import os
import pathlib
import subprocess
import tempfile
import types
import unittest
from unittest import mock

import repository_prep


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

    (repo_path / "file_to_override.c").write_text(
        "original content\n", encoding="utf-8"
    )
    (repo_path / "file_to_patch.c").write_text(
        "original patch content\n", encoding="utf-8"
    )

    subprocess.run(["git", "add", "."], cwd=repo_path, check=True)
    subprocess.run(
        ["git", "commit", "-m", "initial commit"], cwd=repo_path, check=True
    )


class RepositoryPrepTest(unittest.TestCase):
    """Unit tests for util.repository_prep."""

    def test_find_override_files(self):
        """Test that override files are correctly filtered."""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            (temp_path / "file1.c").touch()
            (temp_path / "subdir").mkdir()
            (temp_path / "subdir" / "file2.h").touch()

            (temp_path / ".gitkeep").touch()
            (temp_path / "README.md").touch()
            (temp_path / "OWNERS").touch()
            (temp_path / "DIR_METADATA").touch()
            (temp_path / "LICENSE").touch()
            (temp_path / "subdir" / "README.md").touch()

            files = list(repository_prep.find_override_files(temp_path))
            rel_files = sorted([f.relative_to(temp_path) for f in files])

            self.assertEqual(
                rel_files,
                [
                    pathlib.Path("file1.c"),
                    pathlib.Path("subdir/file2.h"),
                ],
            )

    def test_find_patch_files(self):
        """Test that patch files are correctly found."""
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = pathlib.Path(temp_dir)
            (temp_path / "patch1.patch").touch()
            (temp_path / "subdir").mkdir()
            (temp_path / "subdir" / "patch2.patch").touch()
            (temp_path / "not_a_patch.txt").touch()

            files = list(repository_prep.find_patch_files(temp_path))
            rel_files = sorted([f.relative_to(temp_path) for f in files])

            self.assertEqual(
                rel_files,
                [
                    pathlib.Path("patch1.patch"),
                    pathlib.Path("subdir/patch2.patch"),
                ],
            )

    def test_codebase_preparer_overrides(self):
        """Test applying and reverting source overrides."""
        with tempfile.TemporaryDirectory() as checkout_dir:
            checkout_path = pathlib.Path(checkout_dir)
            ec_path = checkout_path / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            override_root = checkout_path / "overrides"
            override_ec = override_root / "src" / "platform" / "ec-legacy"
            override_ec.mkdir(parents=True)
            (override_ec / "file_to_override.c").write_text(
                "overridden content\n", encoding="utf-8"
            )
            (override_ec / "new_file.c").write_text(
                "new file content\n", encoding="utf-8"
            )

            preparer = repository_prep.CodebasePreparer(
                checkout_path,
                patches_dirs=[],
                src_override_dirs=[override_root],
            )

            preparer.prepare()
            self.assertEqual(
                (ec_path / "file_to_override.c").read_text(encoding="utf-8"),
                "overridden content\n",
            )
            self.assertEqual(
                (ec_path / "new_file.c").read_text(encoding="utf-8"),
                "new file content\n",
            )

            preparer.restore()
            self.assertEqual(
                (ec_path / "file_to_override.c").read_text(encoding="utf-8"),
                "original content\n",
            )
            self.assertFalse((ec_path / "new_file.c").exists())

    def test_codebase_preparer_dirty_check(self):
        """Test that overrides fail if target file is dirty."""
        with tempfile.TemporaryDirectory() as checkout_dir:
            checkout_path = pathlib.Path(checkout_dir)
            ec_path = checkout_path / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            (ec_path / "file_to_override.c").write_text(
                "dirty content\n", encoding="utf-8"
            )

            override_root = checkout_path / "overrides"
            override_ec = override_root / "src" / "platform" / "ec-legacy"
            override_ec.mkdir(parents=True)
            (override_ec / "file_to_override.c").write_text(
                "overridden content\n", encoding="utf-8"
            )

            preparer = repository_prep.CodebasePreparer(
                checkout_path,
                patches_dirs=[],
                src_override_dirs=[override_root],
            )

            with self.assertRaises(repository_prep.RepositoryPrepError):
                preparer.prepare()

    def test_codebase_preparer_patches(self):
        """Test applying and reverting patches."""
        with tempfile.TemporaryDirectory() as checkout_dir:
            checkout_path = pathlib.Path(checkout_dir)
            ec_path = checkout_path / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            patch_dir = checkout_path / "patches"
            patch_ec = patch_dir / "src" / "platform" / "ec-legacy"
            patch_ec.mkdir(parents=True)

            (ec_path / "file_to_patch.c").write_text(
                "original patch content\nmodified patch content\n",
                encoding="utf-8",
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

            preparer = repository_prep.CodebasePreparer(
                checkout_path,
                patches_dirs=[patch_dir],
                src_override_dirs=[],
            )

            preparer.prepare()
            self.assertEqual(
                (ec_path / "file_to_patch.c").read_text(encoding="utf-8"),
                "original patch content\nmodified patch content\n",
            )

            preparer.restore()
            self.assertEqual(
                (ec_path / "file_to_patch.c").read_text(encoding="utf-8"),
                "original patch content\n",
            )

    def test_codebase_preparer_commit(self):
        """Test that commit=True commits changes and skips restore."""
        with tempfile.TemporaryDirectory() as checkout_dir:
            checkout_path = pathlib.Path(checkout_dir)
            ec_path = checkout_path / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            override_root = checkout_path / "overrides"
            override_ec = override_root / "src" / "platform" / "ec-legacy"
            override_ec.mkdir(parents=True)
            (override_ec / "file_to_override.c").write_text(
                "overridden content\n", encoding="utf-8"
            )

            preparer = repository_prep.CodebasePreparer(
                checkout_path,
                patches_dirs=[],
                src_override_dirs=[override_root],
                commit=True,
            )

            preparer.prepare()
            status = subprocess.run(
                ["git", "status", "--porcelain"],
                cwd=ec_path,
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(status.stdout.strip(), "")
            preparer.restore()
            self.assertEqual(
                (ec_path / "file_to_override.c").read_text(encoding="utf-8"),
                "overridden content\n",
            )

    def test_copy_outputs_back_and_cli(self):
        """Test copy_outputs_back merging, lcov path fixing, and CLI main()."""
        with tempfile.TemporaryDirectory() as work_dir:
            work_path = pathlib.Path(work_dir)
            real_co = work_path / "real"
            shadow_co = work_path / "shadow"
            ec_rel = pathlib.Path("src/platform/ec-legacy")

            real_build = real_co / ec_rel / "build"
            (real_build / "board_a").mkdir(parents=True)
            (real_build / "board_a" / "ec.bin").write_text(
                "bin_a\n", encoding="utf-8"
            )

            shadow_build = shadow_co / ec_rel / "build"
            (shadow_build / "board_b").mkdir(parents=True)
            (shadow_build / "board_b" / "ec.bin").write_text(
                "bin_b\n", encoding="utf-8"
            )
            lcov_line = f"SF:{shadow_co}/src/platform/ec-legacy/main.c"
            (shadow_build / "lcov.info").write_text(
                f"{lcov_line}\nend_of_record\n",
                encoding="utf-8",
            )

            repository_prep.copy_outputs_back(shadow_co, real_co, ec_rel)

            self.assertEqual(
                (real_build / "board_a" / "ec.bin").read_text(encoding="utf-8"),
                "bin_a\n",
            )
            self.assertEqual(
                (real_build / "board_b" / "ec.bin").read_text(encoding="utf-8"),
                "bin_b\n",
            )
            expected_lcov = (
                f"SF:{real_co}/src/platform/ec-legacy/main.c\nend_of_record\n"
            )
            self.assertEqual(
                (real_build / "lcov.info").read_text(encoding="utf-8"),
                expected_lcov,
            )

            override_root = work_path / "overrides"
            (override_root / "src" / "platform" / "ec-legacy").mkdir(
                parents=True
            )
            (
                override_root / "src" / "platform" / "ec-legacy" / "foo.c"
            ).write_text("mapped override\n", encoding="utf-8")
            ret = repository_prep.main(
                [
                    "--checkout",
                    str(real_co),
                    "--src-override-dir",
                    str(override_root),
                    "--allow-dirty",
                ]
            )
            self.assertEqual(ret, 0)
            self.assertEqual(
                (real_co / ec_rel / "foo.c").read_text(encoding="utf-8"),
                "mapped override\n",
            )

    def test_create_shadow_checkout_git_abbrev_and_fingerprint(self):
        """Test shadow git repo core.abbrev=10 and fingerprint subdirs."""
        with tempfile.TemporaryDirectory() as work_dir:
            work_path = pathlib.Path(work_dir)
            real_co = work_path / "real"
            shadow_co = work_path / "shadow"

            ec_dir = real_co / "src" / "platform" / "ec-legacy"
            ec_dir.mkdir(parents=True)
            (ec_dir / "main.c").write_text("int main() {}\n", encoding="utf-8")

            fpc = real_co / "src" / "platform" / "fingerprint" / "fpc"
            fpc.mkdir(parents=True)

            repository_prep.create_shadow_checkout(
                real_co, shadow_co, ec_dir, [], []
            )

            shadow_ec = shadow_co / "src" / "platform" / "ec-legacy"
            self.assertTrue(shadow_ec.is_dir() and not shadow_ec.is_symlink())
            short_hash = subprocess.run(
                ["git", "rev-parse", "--short", "--verify", "HEAD"],
                cwd=shadow_ec,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
            self.assertGreaterEqual(len(short_hash), 8)
            self.assertTrue(
                (
                    shadow_co / "src" / "platform" / "fingerprint" / "fpc"
                ).is_symlink()
            )

    def test_ancestor_patches_and_symlinked_git_guard(self):
        """Test ancestor patches, subdir numstat, and symlinked .git guard."""
        with tempfile.TemporaryDirectory() as work_dir:
            work_path = pathlib.Path(work_dir)
            real_co = work_path / "real"
            shadow_co = work_path / "shadow"

            real_fp_root = real_co / "src" / "platform" / "fingerprint"
            real_fpc = real_fp_root / "fpc"
            real_fpc.mkdir(parents=True)
            (real_fpc / "sensor.c").write_text("int s = 1;\n", encoding="utf-8")
            setup_mock_git_repo(real_fp_root)
            real_head_before = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=real_fp_root,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()

            patches_dir = work_path / "patches"
            fp_patch_dir = patches_dir / "src" / "platform" / "fingerprint"
            fp_patch_dir.mkdir(parents=True)
            (fp_patch_dir / "0001-fpc.patch").write_text(
                "--- a/fpc/sensor.c\n"
                "+++ b/fpc/sensor.c\n"
                "@@ -1,1 +1,2 @@\n"
                " int s = 1;\n"
                "+int s2 = 2;\n",
                encoding="utf-8",
            )

            self.assertTrue(
                repository_prep.needs_copy_for_patches(
                    pathlib.Path("src/platform/fingerprint/fpc"), [patches_dir]
                )
            )
            self.assertFalse(
                repository_prep.needs_copy_for_patches(
                    pathlib.Path("src/platform/fingerprint/elan"), [patches_dir]
                )
            )

            sub_patch = work_path / "sub.patch"
            sub_patch.write_text(
                "--- a/sensor.c\n"
                "+++ b/sensor.c\n"
                "@@ -1,1 +1,2 @@\n"
                " int s = 1;\n"
                "+int s2 = 2;\n",
                encoding="utf-8",
            )
            self.assertEqual(
                repository_prep.get_patch_modified_files(sub_patch, real_fpc),
                [pathlib.Path("sensor.c")],
            )

            ec_dir = real_co / "src" / "platform" / "ec-legacy"
            (ec_dir / "build").mkdir(parents=True)
            (ec_dir / "build" / "out.bin").write_text("old\n", encoding="utf-8")
            (ec_dir / "scripts" / "build").mkdir(parents=True)
            (ec_dir / "scripts" / "build" / "helper.py").write_text(
                "# helper\n", encoding="utf-8"
            )
            (ec_dir / "main.c").write_text("int main() {}\n", encoding="utf-8")
            repository_prep.create_shadow_checkout(
                real_co, shadow_co, ec_dir, [], [patches_dir]
            )
            shadow_ec = shadow_co / "src" / "platform" / "ec-legacy"
            self.assertFalse((shadow_ec / "build").exists())
            self.assertTrue(
                (shadow_ec / "scripts" / "build" / "helper.py").is_file()
            )

            shadow_fp_root = shadow_co / "src" / "platform" / "fingerprint"
            os.symlink(real_fp_root / ".git", shadow_fp_root / ".git")

            with repository_prep.CodebasePreparer(
                shadow_co, [patches_dir], [], commit=True
            ):
                self.assertIn(
                    "int s2 = 2;",
                    (shadow_fp_root / "fpc" / "sensor.c").read_text(
                        encoding="utf-8"
                    ),
                )

            real_head_after = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=real_fp_root,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
            self.assertEqual(real_head_after, real_head_before)

    def test_rollback_and_duplicate_override(self):
        """Test rollback on partial failure and duplicate overrides."""
        with tempfile.TemporaryDirectory() as checkout_dir:
            checkout_path = pathlib.Path(checkout_dir)
            ec_path = checkout_path / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            ovr1 = checkout_path / "ovr1"
            ovr2 = checkout_path / "ovr2"
            for idx, ovr in enumerate((ovr1, ovr2), 1):
                (ovr / "src" / "platform" / "ec-legacy").mkdir(parents=True)
                (
                    ovr / "src" / "platform" / "ec-legacy" / "new_file.c"
                ).write_text(f"new {idx}\n", encoding="utf-8")
                (
                    ovr
                    / "src"
                    / "platform"
                    / "ec-legacy"
                    / "file_to_override.c"
                ).write_text(f"ovr {idx}\n", encoding="utf-8")

            with repository_prep.CodebasePreparer(
                checkout_path, [], [ovr1, ovr2]
            ) as preparer:
                self.assertEqual(len(preparer.copied_files), 2)
            self.assertFalse((ec_path / "new_file.c").exists())

            bad_p = checkout_path / "bad"
            (bad_p / "src" / "platform" / "ec-legacy").mkdir(parents=True)
            (
                bad_p / "src" / "platform" / "ec-legacy" / "0001-bad.patch"
            ).write_text(
                "--- a/file_to_patch.c\n"
                "+++ b/file_to_patch.c\n"
                "@@ -1 +1 @@\n-no\n+x\n",
                encoding="utf-8",
            )
            with self.assertRaises(repository_prep.RepositoryPrepError):
                with repository_prep.CodebasePreparer(
                    checkout_path, [bad_p], [ovr1]
                ):
                    pass
            self.assertFalse((ec_path / "new_file.c").exists())
            self.assertEqual(
                (ec_path / "file_to_override.c").read_text(encoding="utf-8"),
                "original content\n",
            )

    def test_fix_symlinks_prefix_boundary(self):
        """Test fix_symlinks respects full directory prefix boundaries."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            build_dir = tmp_path / "build"
            build_dir.mkdir()
            old_prefix = tmp_path / "shadow"
            new_prefix = tmp_path / "real"
            os.symlink(str(old_prefix / "file.c"), build_dir / "match.c")
            os.symlink(
                str(tmp_path / "shadow_other" / "file.c"),
                build_dir / "non_match.c",
            )

            repository_prep.fix_symlinks(build_dir, old_prefix, new_prefix)

            self.assertEqual(
                os.readlink(build_dir / "match.c"),
                str(new_prefix / "file.c"),
            )
            self.assertEqual(
                os.readlink(build_dir / "non_match.c"),
                str(tmp_path / "shadow_other" / "file.c"),
            )

    def test_fix_lcov_info_paths_prefix_boundary(self):
        """Test fix_lcov_info_paths respects directory prefix boundaries."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            build_dir = tmp_path / "build"
            build_dir.mkdir()
            old_prefix = tmp_path / "shadow"
            new_prefix = tmp_path / "real"
            info_file = build_dir / "coverage.info"
            info_file.write_text(
                f"SF:{old_prefix}/main.c\n"
                f"SF:{tmp_path}/shadow_other/main.c\n"
                "end_of_record\n",
                encoding="utf-8",
            )
            repository_prep.fix_lcov_info_paths(
                build_dir, old_prefix, new_prefix
            )
            self.assertEqual(
                info_file.read_text(encoding="utf-8"),
                f"SF:{new_prefix}/main.c\n"
                f"SF:{tmp_path}/shadow_other/main.c\n"
                "end_of_record\n",
            )

    def test_get_dirty_files_untracked_subdir_and_rename(self):
        """Test get_dirty_files reports untracked subdir files and renames."""
        with tempfile.TemporaryDirectory() as tmp:
            checkout = pathlib.Path(tmp).resolve()
            ec_path = checkout / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            untracked_dir = ec_path / "new_subdir"
            untracked_dir.mkdir()
            (untracked_dir / "nested.c").write_text("x\n", encoding="utf-8")
            subprocess.run(
                [
                    "git",
                    "mv",
                    "file_to_override.c",
                    "renamed_file.c",
                ],
                cwd=ec_path,
                check=True,
            )

            dirty = repository_prep.get_dirty_files(ec_path, checkout)
            self.assertIn(
                pathlib.Path("src/platform/ec-legacy/new_subdir/nested.c"),
                dirty,
            )
            self.assertIn(
                pathlib.Path("src/platform/ec-legacy/file_to_override.c"),
                dirty,
            )
            self.assertIn(
                pathlib.Path("src/platform/ec-legacy/renamed_file.c"),
                dirty,
            )

    def test_symlink_override_does_not_corrupt_target(self):
        """Test overriding a symlink replaces link without mutating target."""
        with tempfile.TemporaryDirectory() as tmp:
            checkout = pathlib.Path(tmp).resolve()
            ec_path = checkout / "src" / "platform" / "ec-legacy"
            (ec_path / "base").mkdir(parents=True)
            (ec_path / "board").mkdir(parents=True)
            (ec_path / "base" / "shared.h").write_text(
                "shared original\n", encoding="utf-8"
            )
            os.symlink("../base/shared.h", ec_path / "board" / "gpio.inc")
            setup_mock_git_repo(ec_path)

            ovr_dir = checkout / "ovr"
            (ovr_dir / "src" / "platform" / "ec-legacy" / "board").mkdir(
                parents=True
            )
            (
                ovr_dir
                / "src"
                / "platform"
                / "ec-legacy"
                / "board"
                / "gpio.inc"
            ).write_text("board override\n", encoding="utf-8")

            with repository_prep.CodebasePreparer(checkout, [], [ovr_dir]):
                self.assertFalse((ec_path / "board" / "gpio.inc").is_symlink())
                self.assertEqual(
                    (ec_path / "board" / "gpio.inc").read_text(
                        encoding="utf-8"
                    ),
                    "board override\n",
                )
                self.assertEqual(
                    (ec_path / "base" / "shared.h").read_text(encoding="utf-8"),
                    "shared original\n",
                )

            self.assertTrue((ec_path / "board" / "gpio.inc").is_symlink())
            self.assertEqual(
                (ec_path / "base" / "shared.h").read_text(encoding="utf-8"),
                "shared original\n",
            )

    def test_already_applied_patch_skips_dirty_check_and_relative_dirs(self):
        """Test already-applied patch skips dirty check with relative dirs."""
        with tempfile.TemporaryDirectory() as tmp:
            checkout = pathlib.Path(tmp).resolve()
            ec_path = checkout / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            patch_dir = checkout / "rel_patches"
            patch_ec = patch_dir / "src" / "platform" / "ec-legacy"
            patch_ec.mkdir(parents=True)
            (ec_path / "file_to_patch.c").write_text(
                "original patch content\nadded line\n", encoding="utf-8"
            )
            diff = subprocess.run(
                ["git", "diff"],
                cwd=ec_path,
                capture_output=True,
                text=True,
                check=True,
            ).stdout
            (patch_ec / "0001-add.patch").write_text(diff, encoding="utf-8")

            saved_cwd = os.getcwd()
            try:
                os.chdir(checkout)
                preparer = repository_prep.CodebasePreparer(
                    checkout,
                    patches_dirs=[pathlib.Path("rel_patches")],
                    src_override_dirs=[],
                )
                preparer.prepare()
                self.assertEqual(preparer.applied_patches, [])
            finally:
                os.chdir(saved_cwd)

    def test_prepared_workspace_shadow_and_failure_guard(self):
        """Test prepared_workspace shadow mode and shadow_ready guard."""
        with tempfile.TemporaryDirectory() as tmp:
            checkout = pathlib.Path(tmp).resolve()
            (checkout / ".repo").mkdir()
            ec_path = checkout / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            ovr_dir = checkout / "ovr"
            (ovr_dir / "src" / "platform" / "ec-legacy").mkdir(parents=True)
            (
                ovr_dir
                / "src"
                / "platform"
                / "ec-legacy"
                / "file_to_override.c"
            ).write_text("shadow ovr\n", encoding="utf-8")

            prev_co = repository_prep.set_checkout(checkout)
            try:
                opts = types.SimpleNamespace(
                    patches_dirs=[],
                    src_override_dirs=[ovr_dir],
                    no_shadow=False,
                    func=lambda: None,
                )
                with repository_prep.prepared_workspace(
                    opts, ec_dir=ec_path
                ) as shadow_ec:
                    self.assertIsNotNone(shadow_ec)
                    self.assertNotEqual(shadow_ec.resolve(), ec_path.resolve())
                    self.assertEqual(
                        (shadow_ec / "file_to_override.c").read_text(
                            encoding="utf-8"
                        ),
                        "shadow ovr\n",
                    )
                    (shadow_ec / "build").mkdir()
                    (shadow_ec / "build" / "out.bin").write_text(
                        "built\n", encoding="utf-8"
                    )
                self.assertEqual(
                    (ec_path / "build" / "out.bin").read_text(encoding="utf-8"),
                    "built\n",
                )

                # Dirty target file in real checkout should abort before yield
                # and NOT call copy_outputs_back.
                (ec_path / "file_to_override.c").write_text(
                    "local dirty\n", encoding="utf-8"
                )
                with mock.patch.object(
                    repository_prep, "copy_outputs_back"
                ) as mock_copy:
                    with self.assertRaises(repository_prep.RepositoryPrepError):
                        with repository_prep.prepared_workspace(
                            opts, ec_dir=ec_path
                        ):
                            pass
                    mock_copy.assert_not_called()
            finally:
                repository_prep.set_checkout(prev_co)

    def test_round2_edge_cases(self):
        """Test rename patches, dirty symlinks, mapped bounds, and errors."""
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp).resolve()
            checkout = root / "checkout"
            ec_path = checkout / "src" / "platform" / "ec-legacy"
            ec_path.mkdir(parents=True)
            setup_mock_git_repo(ec_path)

            # 1. Dirty symlink detection in get_dirty_files()
            os.symlink("file_to_override.c", ec_path / "dirty_link.c")
            dirty = repository_prep.get_dirty_files(ec_path, checkout)
            self.assertIn(
                pathlib.Path("src/platform/ec-legacy/dirty_link.c"), dirty
            )
            (ec_path / "dirty_link.c").unlink()

            # 2. get_patch_modified_files() with rename from and quoted paths
            rename_patch = root / "rename.patch"
            rename_patch.write_text(
                'diff --git "a/sub/a\\"b.c" "b/sub/c\\"d.c"\n'
                "similarity index 100%\n"
                'rename from "sub/a\\"b.c"\n'
                'rename to "sub/c\\"d.c"\n',
                encoding="utf-8",
            )
            self.assertEqual(
                repository_prep.get_patch_modified_files(rename_patch, ec_path),
                [pathlib.Path('sub/c"d.c')],
            )

            # 3. revert_source_overrides() bound check with external mappings
            ext_root = root / "external_ec"
            ext_root.mkdir()
            ovr_dir = root / "ovr"
            (ovr_dir / "src" / "platform" / "ec-legacy" / "nested").mkdir(
                parents=True
            )
            (
                ovr_dir
                / "src"
                / "platform"
                / "ec-legacy"
                / "nested"
                / "added.c"
            ).write_text("ext\n", encoding="utf-8")
            with repository_prep.CodebasePreparer(
                checkout,
                [],
                [ovr_dir],
                path_mappings={"src/platform/ec-legacy": ext_root},
                allow_dirty=True,
            ):
                self.assertTrue((ext_root / "nested" / "added.c").is_file())
            self.assertFalse((ext_root / "nested").exists())
            self.assertTrue(ext_root.is_dir())

            # 4. commit_changes() raises RepositoryPrepError on git failure
            with mock.patch("subprocess.run") as mock_run:
                mock_run.side_effect = subprocess.CalledProcessError(
                    1, ["git", "add"], stderr=b"mock git failure"
                )
                preparer = repository_prep.CodebasePreparer(
                    checkout, [], [], commit=True
                )
                with mock.patch.object(
                    repository_prep, "find_target_repos", return_value={ec_path}
                ):
                    with self.assertRaises(repository_prep.RepositoryPrepError):
                        preparer.commit_changes()


if __name__ == "__main__":
    unittest.main()
