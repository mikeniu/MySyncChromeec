# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for codebase preparation (patches and overrides)."""

import argparse
import os
import pathlib
import subprocess
from unittest import mock

import pytest
from zmake.repository_prep import _find_override_files
from zmake.repository_prep import _find_patch_files
from zmake.repository_prep import _has_files
from zmake.repository_prep import CodebasePreparer
from zmake.repository_prep import copy_outputs_back
from zmake.repository_prep import create_shadow_checkout
from zmake.repository_prep import find_git_repo
from zmake.repository_prep import fix_symlinks
from zmake.repository_prep import get_dirty_files
from zmake.repository_prep import get_patch_modified_files
from zmake.repository_prep import main as prep_main
from zmake.repository_prep import needs_copy_for_patches
from zmake.repository_prep import prepared_workspace
from zmake.repository_prep import RepositoryPrepError
from zmake.repository_prep import set_checkout


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


@pytest.fixture(name="cros_checkout")
def fixture_cros_checkout(tmp_path, setup_git_repo):
    """Create a mock ChromeOS checkout with initialized EC repo and Zephyr base."""
    real_co = tmp_path / "cros"
    (real_co / ".repo").mkdir(parents=True)
    ec_path = real_co / "src" / "platform" / "ec"
    ec_path.mkdir(parents=True)
    setup_git_repo(ec_path)
    zephyr_base = real_co / "src" / "third_party" / "zephyrproject" / "zephyr"
    zephyr_base.mkdir(parents=True)
    (zephyr_base / "Kconfig").write_text("mainmenu\n", encoding="utf-8")
    return real_co, ec_path, zephyr_base


def test_codebase_preparer_commit(cros_checkout, tmp_path):
    """Test that commit=True commits changes and skips restore."""
    real_co, ec_path, _ = cros_checkout
    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "file_to_override.c").write_text(
        "overridden content\n", encoding="utf-8"
    )

    preparer = CodebasePreparer(
        real_co,
        patches_dirs=[],
        src_override_dirs=[override_root],
        commit=True,
    )
    preparer.prepare()

    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "overridden content\n"
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=ec_path,
        capture_output=True,
        text=True,
        check=True,
    )
    assert status.stdout.strip() == ""
    log = subprocess.run(
        ["git", "log", "-n", "1", "--format=%s"],
        cwd=ec_path,
        capture_output=True,
        text=True,
        check=True,
    )
    assert log.stdout.strip() == "applied patches and overrides"

    preparer.restore()
    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "overridden content\n"


def test_has_files_ignores_metadata(tmp_path):
    """Test that _has_files ignores .gitkeep, .keep, README.md, OWNERS, etc."""
    assert not _has_files(tmp_path)
    for name in (
        ".gitkeep",
        ".keep",
        "README.md",
        "OWNERS",
        "DIR_METADATA",
        "LICENSE",
    ):
        (tmp_path / name).touch()
    assert not _has_files(tmp_path)

    (tmp_path / "real_patch.patch").touch()
    assert _has_files(tmp_path)


def test_codebase_preparer_path_mappings_and_cli(tmp_path):
    """Test path_mappings in non-git ebuild layout and standalone CLI main()."""
    ec_mod = tmp_path / "zephyrproject" / "modules" / "ec"
    ec_mod.mkdir(parents=True)
    (ec_mod / "foo.c").write_text("base\n", encoding="utf-8")

    override_root = tmp_path / "overrides"
    (override_root / "src" / "platform" / "ec").mkdir(parents=True)
    (override_root / "src" / "platform" / "ec" / "foo.c").write_text(
        "mapped override\n", encoding="utf-8"
    )

    CodebasePreparer(
        tmp_path,
        patches_dirs=[],
        src_override_dirs=[override_root],
        path_mappings={"src/platform/ec": ec_mod},
        allow_dirty=True,
    ).prepare()
    assert (ec_mod / "foo.c").read_text(encoding="utf-8") == "mapped override\n"

    checkout_dir = tmp_path / "direct_checkout"
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
    assert (checkout_dir / "src" / "platform" / "ec" / "foo.c").read_text(
        encoding="utf-8"
    ) == "mapped override\n"
    assert (checkout_dir / ".workspace_prepared").exists()


def test_rollback_on_partial_failure_and_duplicate_override(
    cros_checkout, tmp_path
):
    """Test __enter__ rollback on partial failure and duplicate override deduplication."""
    real_co, ec_path, _ = cros_checkout
    ovr1, ovr2 = tmp_path / "ovr1", tmp_path / "ovr2"
    for idx, ovr in enumerate((ovr1, ovr2), 1):
        (ovr / "src" / "platform" / "ec").mkdir(parents=True)
        (ovr / "src" / "platform" / "ec" / "new_file.c").write_text(
            f"new content {idx}\n", encoding="utf-8"
        )
        (ovr / "src" / "platform" / "ec" / "file_to_override.c").write_text(
            f"override {idx}\n", encoding="utf-8"
        )

    with CodebasePreparer(real_co, [], [ovr1, ovr2]) as preparer:
        assert len(preparer.copied_files) == 2
        assert (ec_path / "new_file.c").read_text(
            encoding="utf-8"
        ) == "new content 2\n"
    assert not (ec_path / "new_file.c").exists()
    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "original content\n"

    bad_patches = tmp_path / "bad_patches"
    (bad_patches / "src" / "platform" / "ec").mkdir(parents=True)
    (bad_patches / "src" / "platform" / "ec" / "0001-bad.patch").write_text(
        "--- a/file_to_patch.c\n+++ b/file_to_patch.c\n@@ -1 +1 @@\n-nope\n+bad\n",
        encoding="utf-8",
    )
    with pytest.raises(RepositoryPrepError):
        with CodebasePreparer(real_co, [bad_patches], [ovr1]):
            pass
    assert not (ec_path / "new_file.c").exists()
    assert (ec_path / "file_to_override.c").read_text(
        encoding="utf-8"
    ) == "original content\n"


def test_symlink_override_safety(cros_checkout, tmp_path):
    """Test overriding a symlink replaces link without corrupting target."""
    real_co, ec_path, _ = cros_checkout
    shared_hdr = ec_path / "shared.h"
    shared_hdr.write_text("shared content\n", encoding="utf-8")
    link_hdr = ec_path / "board_link.h"
    os.symlink("shared.h", link_hdr)
    subprocess.run(
        ["git", "add", "shared.h", "board_link.h"], cwd=ec_path, check=True
    )
    subprocess.run(
        ["git", "commit", "-m", "add symlink"], cwd=ec_path, check=True
    )

    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "board_link.h").write_text(
        "overridden link\n", encoding="utf-8"
    )

    with CodebasePreparer(real_co, [], [override_root]):
        assert not link_hdr.is_symlink()
        assert link_hdr.read_text(encoding="utf-8") == "overridden link\n"
        assert shared_hdr.read_text(encoding="utf-8") == "shared content\n"

    assert link_hdr.is_symlink()
    assert shared_hdr.read_text(encoding="utf-8") == "shared content\n"


def test_preparer_edge_cases(cros_checkout, tmp_path, setup_git_repo):
    """Test symlinks in get_dirty_files, numstat -z/renames/subdir, and bounds."""
    real_co, ec_path, _ = cros_checkout

    # 1. get_dirty_files preserves symlink names and find_git_repo follows dir symlinks
    (ec_path / "target.h").write_text("hdr\n", encoding="utf-8")
    os.symlink("target.h", ec_path / "sym.h")
    subprocess.run(["git", "add", "target.h", "sym.h"], cwd=ec_path, check=True)
    subprocess.run(["git", "commit", "-m", "sym"], cwd=ec_path, check=True)
    (ec_path / "sym.h").unlink()
    os.symlink("other.h", ec_path / "sym.h")
    dirty = get_dirty_files(ec_path, real_co)
    assert pathlib.Path("src/platform/ec/sym.h") in dirty

    symlinked_ec = tmp_path / "symlinked_ec"
    os.symlink(ec_path, symlinked_ec)
    assert find_git_repo(symlinked_ec / "target.h") == ec_path.resolve()

    ext_zephyr = tmp_path / "ext_zephyr"
    ext_zephyr.mkdir()
    setup_git_repo(ext_zephyr)
    (ext_zephyr / "file_to_patch.c").write_text("dirty ext\n", encoding="utf-8")
    zp_rel = pathlib.Path("src/third_party/zephyrproject/zephyr")
    ext_dirty = get_dirty_files(ext_zephyr, real_co, {zp_rel: ext_zephyr})
    assert zp_rel / "file_to_patch.c" in ext_dirty

    # 2. get_patch_modified_files handles renames (-z) and subdirectories
    rename_patch = tmp_path / "rename.patch"
    rename_patch.write_text(
        "diff --git a/dir/file_to_patch.c b/dir/renamed.c\n"
        "similarity index 100%\n"
        "rename from dir/file_to_patch.c\n"
        "rename to dir/renamed.c\n",
        encoding="utf-8",
    )
    mod_files = get_patch_modified_files(rename_patch, ec_path)
    assert mod_files == [pathlib.Path("dir/renamed.c")]

    sub_dir = ec_path / "sub" / "kernel"
    sub_dir.mkdir(parents=True)
    sub_patch = tmp_path / "sub.patch"
    sub_patch.write_text(
        "--- a/kernel/version.c\n"
        "+++ b/kernel/version.c\n"
        "@@ -1,1 +1,2 @@\n"
        " int v = 1;\n"
        "+int v2 = 2;\n",
        encoding="utf-8",
    )
    assert get_patch_modified_files(sub_patch, ec_path / "sub") == [
        pathlib.Path("kernel/version.c")
    ]

    # 3. revert_source_overrides bounds parent directory cleanup via path_mappings
    ext_mod = tmp_path / "ext_mod"
    ext_mod.mkdir()
    mod_ovr = tmp_path / "mod_ovr"
    (mod_ovr / "src" / "platform" / "ec" / "nested").mkdir(parents=True)
    (mod_ovr / "src" / "platform" / "ec" / "nested" / "new.c").write_text(
        "x\n", encoding="utf-8"
    )
    with CodebasePreparer(
        real_co,
        [],
        [mod_ovr],
        path_mappings={pathlib.Path("src/platform/ec"): ext_mod},
    ):
        assert (ext_mod / "nested" / "new.c").exists()
    assert ext_mod.exists()
    assert not (ext_mod / "nested").exists()

    # 4. commit_changes raises RepositoryPrepError on git failure and skips symlinked .git
    ovr_dir = tmp_path / "ovr"
    (ovr_dir / "src" / "platform" / "ec").mkdir(parents=True)
    (ovr_dir / "src" / "platform" / "ec" / "ext.c").write_text(
        "ext ovr\n", encoding="utf-8"
    )
    prep = CodebasePreparer(real_co, [], [ovr_dir], commit=True)
    with mock.patch(
        "subprocess.run",
        side_effect=subprocess.CalledProcessError(
            1, ["git", "add"], stderr="mock err"
        ),
    ):
        with pytest.raises(RepositoryPrepError):
            prep.commit_changes()


def test_copy_outputs_back_merges_and_fixes_lcov_paths(tmp_path):
    """Test copy_outputs_back merges build dirs and fixes lcov paths cleanly."""
    real_co, shadow_co = tmp_path / "real", tmp_path / "shadow"
    ec_rel = pathlib.Path("src/platform/ec")

    real_build = real_co / ec_rel / "build"
    (real_build / "board_a").mkdir(parents=True)
    (real_build / "board_a" / "ec.bin").write_text("bin_a\n", encoding="utf-8")
    (real_build / "sym_dir").mkdir(parents=True)
    (real_build / "sym_dir" / "old.txt").write_text("old\n", encoding="utf-8")

    shadow_build = shadow_co / ec_rel / "build"
    (shadow_build / "board_b").mkdir(parents=True)
    (shadow_build / "board_b" / "ec.bin").write_text(
        "bin_b\n", encoding="utf-8"
    )
    os.symlink(real_build / "board_a", shadow_build / "sym_dir")
    (shadow_build / "lcov.info").write_text(
        f"SF:{shadow_co}/src/platform/ec/main.c\n"
        f"SF:{shadow_co}_other/src/platform/ec/main.c\n"
        "end_of_record\n",
        encoding="utf-8",
    )
    (shadow_co / ec_rel / "twister-out.json").write_text(
        "{}\n", encoding="utf-8"
    )

    copy_outputs_back(shadow_co, real_co, ec_rel)

    assert (real_build / "board_a" / "ec.bin").read_text(
        encoding="utf-8"
    ) == "bin_a\n"
    assert (real_build / "board_b" / "ec.bin").read_text(
        encoding="utf-8"
    ) == "bin_b\n"
    assert (real_build / "sym_dir").is_symlink()
    assert (real_co / ec_rel / "twister-out.json").read_text(
        encoding="utf-8"
    ) == "{}\n"
    assert (real_build / "lcov.info").read_text(encoding="utf-8") == (
        f"SF:{real_co}/src/platform/ec/main.c\n"
        f"SF:{shadow_co}_other/src/platform/ec/main.c\n"
        "end_of_record\n"
    )


def test_create_shadow_checkout_and_fix_symlinks(cros_checkout, tmp_path):
    """Test create_shadow_checkout deduplicates, expands fingerprint, and fix_symlinks."""
    real_co, ec_dir, zephyr_base = cros_checkout
    shadow_co = tmp_path / "shadow"

    (ec_dir / "build").mkdir()
    (ec_dir / "build" / "old.o").write_text("ignored\n", encoding="utf-8")
    (ec_dir / "scripts" / "build").mkdir(parents=True)
    (ec_dir / "scripts" / "build" / "helper.py").write_text(
        "kept\n", encoding="utf-8"
    )
    (ec_dir / "twister-out-host.2").mkdir()
    (ec_dir / "twister-out-host.2" / "log.txt").write_text(
        "ignored\n", encoding="utf-8"
    )

    boringssl = real_co / "src" / "third_party" / "boringssl"
    boringssl.mkdir(parents=True)
    egis_fp = real_co / "src" / "platform" / "fingerprint" / "egis"
    egis_fp.mkdir(parents=True)
    (real_co / "src" / "platform" / "fingerprint" / "google-fpalg").mkdir(
        parents=True
    )

    modules = {"ec": ec_dir, "boringssl": boringssl, "egis_fp": egis_fp}
    create_shadow_checkout(real_co, shadow_co, modules, zephyr_base, [], [])

    shadow_ec = shadow_co / "src" / "platform" / "ec"
    assert shadow_ec.is_dir() and not shadow_ec.is_symlink()
    assert not (shadow_ec / "build").exists()
    assert (shadow_ec / "scripts" / "build" / "helper.py").is_file()
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

    # External zephyr_base with overrides is copied into shadow_co / zp_rel
    ext_zephyr = tmp_path / "external_zephyr"
    ext_zephyr.mkdir()
    (ext_zephyr / "Kconfig").touch()
    zp_rel = pathlib.Path("src/third_party/zephyrproject/zephyr")
    ovr_dir = tmp_path / "ovr"
    (ovr_dir / zp_rel).mkdir(parents=True)
    (ovr_dir / zp_rel / "ext.c").write_text("ext ovr\n", encoding="utf-8")
    shadow_co_ext = tmp_path / "shadow_ext"
    create_shadow_checkout(
        real_co, shadow_co_ext, modules, ext_zephyr, [ovr_dir], []
    )
    assert (shadow_co_ext / zp_rel).is_dir() and not (
        shadow_co_ext / zp_rel
    ).is_symlink()

    # fix_symlinks path-boundary safety
    sym_dir = tmp_path / "sym_test"
    sym_dir.mkdir()
    os.symlink("/tmp/shadow/file.c", sym_dir / "match_link")
    os.symlink("/tmp/shadow_other/file.c", sym_dir / "non_match_link")
    fix_symlinks(sym_dir, "/tmp/shadow", "/tmp/real")
    assert os.readlink(sym_dir / "match_link") == "/tmp/real/file.c"
    assert os.readlink(sym_dir / "non_match_link") == "/tmp/shadow_other/file.c"


def test_ancestor_patches_and_symlinked_git_guard(
    cros_checkout, tmp_path, setup_git_repo
):
    """Test ancestor patches and symlinked .git guard in shadow checkout."""
    real_co, ec_dir, real_zephyr = cros_checkout
    shadow_co = tmp_path / "shadow"
    real_zp = real_zephyr.parent
    (real_zephyr / "kernel").mkdir(parents=True)
    (real_zephyr / "kernel" / "version.c").write_text(
        "int v = 1;\n", encoding="utf-8"
    )
    setup_git_repo(real_zp)
    real_head_before = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=real_zp,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()

    patches_dir = tmp_path / "patches"
    zp_patch_dir = patches_dir / "src" / "third_party" / "zephyrproject"
    zp_patch_dir.mkdir(parents=True)
    (zp_patch_dir / "0001-ver.patch").write_text(
        "--- a/zephyr/kernel/version.c\n"
        "+++ b/zephyr/kernel/version.c\n"
        "@@ -1,1 +1,2 @@\n"
        " int v = 1;\n"
        "+int v2 = 2;\n",
        encoding="utf-8",
    )

    assert needs_copy_for_patches(
        pathlib.Path("src/third_party/zephyrproject/zephyr"), [patches_dir]
    )
    assert not needs_copy_for_patches(
        pathlib.Path("src/third_party/zephyrproject/modules/hal/stm32"),
        [patches_dir],
    )

    create_shadow_checkout(
        real_co, shadow_co, {"ec": ec_dir}, real_zephyr, [], [patches_dir]
    )

    shadow_zp = shadow_co / "src" / "third_party" / "zephyrproject"
    shadow_zephyr = shadow_zp / "zephyr"
    assert not (shadow_zp / ".git").exists()
    assert shadow_zephyr.is_dir() and not shadow_zephyr.is_symlink()

    os.symlink(real_zp / ".git", shadow_zp / ".git")
    with CodebasePreparer(shadow_co, [patches_dir], [], commit=True):
        assert "int v2 = 2;" in (
            shadow_zephyr / "kernel" / "version.c"
        ).read_text(encoding="utf-8")

    real_head_after = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=real_zp,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    assert real_head_after == real_head_before
    assert (real_zephyr / "kernel" / "version.c").read_text(
        encoding="utf-8"
    ) == "int v = 1;\n"
    assert os.path.exists(shadow_zephyr / ".git")


def test_prepared_workspace_shadow_and_partial_failure(
    cros_checkout, tmp_path, monkeypatch
):
    """Test prepared_workspace shadow execution, CLI option remapping, and partial failure."""
    real_co, ec_path, zephyr_base = cros_checkout
    override_root = tmp_path / "overrides"
    override_ec = override_root / "src" / "platform" / "ec"
    override_ec.mkdir(parents=True)
    (override_ec / "file_to_override.c").write_text(
        "shadow override\n", encoding="utf-8"
    )

    old_co = set_checkout(real_co)
    monkeypatch.delenv("ZMAKE_WORKSPACE_PREPARED", raising=False)
    try:
        opts = argparse.Namespace(
            checkout=real_co,
            patches_dirs=[],
            src_override_dirs=[override_root],
            no_shadow=False,
            zephyr_base=zephyr_base,
            projects_dirs=[ec_path / "zephyr"],
            modules_dir=None,
            func=lambda _o: 0,
        )
        with prepared_workspace(
            opts, modules={"ec": ec_path}, checkout=real_co
        ) as shadow_ec:
            assert shadow_ec is not None
            assert os.environ.get("ZMAKE_WORKSPACE_PREPARED") == "1"
            assert opts.checkout == shadow_ec.parents[2]
            assert opts.zephyr_base.is_relative_to(opts.checkout)
            assert opts.projects_dirs[0] == shadow_ec / "zephyr"
            assert (shadow_ec / "file_to_override.c").read_text(
                encoding="utf-8"
            ) == "shadow override\n"
            (shadow_ec / "build").mkdir()
            (shadow_ec / "build" / "out.bin").write_text(
                "artifact\n", encoding="utf-8"
            )
            (shadow_ec / "twister-out-llvm").mkdir()
            (shadow_ec / "twister-out-llvm" / "coverage.info").write_text(
                "cov\n", encoding="utf-8"
            )

        assert os.environ.get("ZMAKE_WORKSPACE_PREPARED") is None
        assert (ec_path / "build" / "out.bin").read_text(
            encoding="utf-8"
        ) == "artifact\n"
        assert (ec_path / "twister-out-llvm" / "coverage.info").read_text(
            encoding="utf-8"
        ) == "cov\n"

        # bundle and merge_shards prepare real_co in-place so genhtml sees
        # patched/overridden files at canonical real_co paths.
        def bundle(_o):
            return 0

        def merge_shards(_o):
            return 0

        for fn in (bundle, merge_shards):
            opts.func = fn
            with prepared_workspace(opts, modules={"ec": ec_path}) as shadow_ec:
                assert shadow_ec is None
                assert os.environ.get("ZMAKE_WORKSPACE_PREPARED") == "1"
                assert (ec_path / "file_to_override.c").read_text(
                    encoding="utf-8"
                ) == "shadow override\n"
            assert (ec_path / "file_to_override.c").read_text(
                encoding="utf-8"
            ) == "original content\n"

        # Dirty file aborts before shadow_ready=True; copy_outputs_back is not called
        opts.func = lambda _o: 0
        (ec_path / "file_to_override.c").write_text(
            "local dirty\n", encoding="utf-8"
        )
        with pytest.raises(RepositoryPrepError):
            with prepared_workspace(opts, modules={"ec": ec_path}):
                pass
    finally:
        set_checkout(old_co)
