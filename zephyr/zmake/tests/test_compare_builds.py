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
