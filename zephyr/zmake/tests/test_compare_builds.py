# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake compare-builds codebase checkouts."""

from unittest import mock

import zmake.compare_builds
import zmake.modules
import zmake.multiproc


def test_compare_builds_checkouts(tmp_path, monkeypatch, setup_git_repo):
    """Test that CompareBuilds clones and checks out correct refs."""
    ec_src_dir = tmp_path / "ec_src"
    ec_src_dir.mkdir()
    ec_commits = setup_git_repo(
        ec_src_dir,
        [
            {
                "file1.txt": "v1 content\n",
                "zephyr/program/proj1/BUILD.py": "# proj1 v1\n",
            },
            {
                "file1.txt": "v2 content\n",
                "zephyr/program/proj1/BUILD.py": "# proj1 v2\n",
            },
        ],
    )
    ref1, ref2 = ec_commits[0], ec_commits[1]

    zephyr_src_dir = tmp_path / "zephyr_src"
    zephyr_src_dir.mkdir()
    setup_git_repo(zephyr_src_dir, [{"VERSION": "VERSION_MAJOR = 3\n"}])

    executor = mock.Mock(spec=zmake.multiproc.Executor)
    executor.append.side_effect = lambda func: func()
    executor.wait.return_value = 0
    mock_known_modules = {
        "ec": lambda name, checkout: checkout / "src" / "platform" / "ec"
    }

    with mock.patch.dict(
        "zmake.modules.known_modules", mock_known_modules, clear=True
    ):
        monkeypatch.chdir(ec_src_dir)
        cmp_dir = tmp_path / "compare_work"
        cmp_dir.mkdir()

        cmp_builds = zmake.compare_builds.CompareBuilds(
            temp_dir=str(cmp_dir),
            ref1=ref1,
            ref2=ref2,
            executor=executor,
            sequential=True,
            target_module="ec",
            target_module_path=ec_src_dir,
        )

        assert len(cmp_builds.checkouts) == 2
        assert cmp_builds.checkouts[0].ref == ref1
        assert cmp_builds.checkouts[1].ref == ref2

        module_paths = {"ec": ec_src_dir}
        cmp_builds.do_checkouts(
            zephyr_base=zephyr_src_dir,
            module_paths=module_paths,
        )

        checkout1 = cmp_builds.checkouts[0]
        checkout2 = cmp_builds.checkouts[1]

        ec_checkout1 = checkout1.modules_dir / "ec"
        ec_checkout2 = checkout2.modules_dir / "ec"

        assert (ec_checkout1 / "file1.txt").read_text(
            encoding="utf-8"
        ) == "v1 content\n"
        assert (ec_checkout2 / "file1.txt").read_text(
            encoding="utf-8"
        ) == "v2 content\n"

        zephyr_checkout1 = checkout1.zephyr_dir
        zephyr_checkout2 = checkout2.zephyr_dir

        assert (zephyr_checkout1 / "VERSION").exists()
        assert (zephyr_checkout2 / "VERSION").exists()


def test_compare_builds_checkouts_with_patches_and_overrides(
    tmp_path, monkeypatch, setup_git_repo
):
    """Test CompareBuilds prepares each ref worktree without mutating sources."""
    ec_src_dir = tmp_path / "ec_src"
    ec_src_dir.mkdir()
    ec_commits = setup_git_repo(
        ec_src_dir,
        [
            {
                "file1.txt": "v1 content\n",
                "upstream_overrides/src-override/src/platform/ec/up.txt": (
                    "upstream v1\n"
                ),
            },
            {
                "file1.txt": "v2 content\n",
                "upstream_overrides/src-override/src/platform/ec/up.txt": (
                    "upstream v2\n"
                ),
            },
        ],
    )
    ref1, ref2 = ec_commits[0], ec_commits[1]

    zephyr_src_dir = tmp_path / "zephyr_src"
    zephyr_src_dir.mkdir()
    setup_git_repo(zephyr_src_dir, [{"VERSION": "VERSION_MAJOR = 3\n"}])

    priv_overrides = tmp_path / "priv_overrides"
    priv_ec_file = priv_overrides / "src" / "platform" / "ec" / "priv.txt"
    priv_ec_file.parent.mkdir(parents=True)
    priv_ec_file.write_text("private content\n", encoding="utf-8")

    priv_patches = tmp_path / "priv_patches"
    zephyr_patch_dir = (
        priv_patches / "src" / "third_party" / "zephyrproject" / "zephyr"
    )
    zephyr_patch_dir.mkdir(parents=True)
    (zephyr_patch_dir / "0001-version.patch").write_text(
        "--- a/VERSION\n"
        "+++ b/VERSION\n"
        "@@ -1 +1 @@\n"
        "-VERSION_MAJOR = 3\n"
        "+VERSION_MAJOR = 4\n",
        encoding="utf-8",
    )

    executor = mock.Mock(spec=zmake.multiproc.Executor)
    executor.append.side_effect = lambda func: func()
    executor.wait.return_value = 0
    mock_known_modules = {
        "ec": lambda name, checkout: checkout / "src" / "platform" / "ec"
    }

    with mock.patch.dict(
        "zmake.modules.known_modules", mock_known_modules, clear=True
    ):
        monkeypatch.chdir(ec_src_dir)
        cmp_dir = tmp_path / "compare_work"
        cmp_dir.mkdir()

        cmp_builds = zmake.compare_builds.CompareBuilds(
            temp_dir=str(cmp_dir),
            ref1=ref1,
            ref2=ref2,
            executor=executor,
            sequential=True,
            target_module="ec",
            target_module_path=ec_src_dir,
        )

        # Dirty uncommitted file in live working tree must not abort compare-builds
        (ec_src_dir / "up.txt").write_text(
            "dirty live working tree\n", encoding="utf-8"
        )

        cmp_builds.do_checkouts(
            zephyr_base=zephyr_src_dir,
            module_paths={"ec": ec_src_dir},
            patches_dirs=[priv_patches],
            src_override_dirs=[
                ec_src_dir / "upstream_overrides" / "src-override",
                priv_overrides,
            ],
        )

        checkout1 = cmp_builds.checkouts[0]
        checkout2 = cmp_builds.checkouts[1]
        ec_checkout1 = checkout1.modules_dir / "ec"
        ec_checkout2 = checkout2.modules_dir / "ec"

        assert (ec_checkout1 / "file1.txt").read_text(
            encoding="utf-8"
        ) == "v1 content\n"
        assert (ec_checkout2 / "file1.txt").read_text(
            encoding="utf-8"
        ) == "v2 content\n"
        assert (ec_checkout1 / "up.txt").read_text(
            encoding="utf-8"
        ) == "upstream v1\n"
        assert (ec_checkout2 / "up.txt").read_text(
            encoding="utf-8"
        ) == "upstream v2\n"
        assert (ec_checkout1 / "priv.txt").read_text(
            encoding="utf-8"
        ) == "private content\n"
        assert (ec_checkout2 / "priv.txt").read_text(
            encoding="utf-8"
        ) == "private content\n"
        assert (checkout1.zephyr_dir / "VERSION").read_text(
            encoding="utf-8"
        ) == "VERSION_MAJOR = 4\n"
        assert (checkout2.zephyr_dir / "VERSION").read_text(
            encoding="utf-8"
        ) == "VERSION_MAJOR = 4\n"

        # Original repos must remain clean and unmutated
        assert not (ec_src_dir / "priv.txt").exists()
        assert (zephyr_src_dir / "VERSION").read_text(
            encoding="utf-8"
        ) == "VERSION_MAJOR = 3\n"
