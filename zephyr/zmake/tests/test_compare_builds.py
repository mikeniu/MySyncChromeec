# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for zmake compare-builds codebase checkouts."""

import os
import pathlib
import subprocess
import tempfile
from unittest import mock

import zmake.compare_builds
import zmake.modules
import zmake.multiproc


def setup_mock_git_repo(repo_dir, files_contents):
    """Initialize a git repo and make commits.

    Args:
        repo_dir: Directory to initialize.
        files_contents: List of dicts, each representing a commit.
            Each dict maps filename to content.
    """
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

    commits = []
    for i, commit_files in enumerate(files_contents):
        for fname, content in commit_files.items():
            fpath = repo_path / fname
            fpath.parent.mkdir(parents=True, exist_ok=True)
            fpath.write_text(content)
        subprocess.run(["git", "add", "."], cwd=repo_path, check=True)
        subprocess.run(
            ["git", "commit", "-m", f"commit {i}"], cwd=repo_path, check=True
        )
        # Get the commit hash
        hash_res = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_path,
            check=True,
            capture_output=True,
            text=True,
        )
        commits.append(hash_res.stdout.strip())

    return commits


def test_compare_builds_checkouts():
    """Test that CompareBuilds clones and checks out correct refs."""
    with tempfile.TemporaryDirectory() as temp_dir_name:
        temp_dir = pathlib.Path(temp_dir_name)

        # 1. Setup fake EC repo with 2 commits
        ec_src_dir = temp_dir / "ec_src"
        ec_src_dir.mkdir()
        ec_commits = setup_mock_git_repo(
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

        # 2. Setup fake Zephyr repo (needed for clone in do_checkouts)
        zephyr_src_dir = temp_dir / "zephyr_src"
        zephyr_src_dir.mkdir()
        setup_mock_git_repo(
            zephyr_src_dir, [{"VERSION": "VERSION_MAJOR = 3\n"}]
        )

        # We need to run CompareBuilds in a CWD that is a git repo if we don't mock get_git_hash.
        # CompareBuilds.__init__ calls get_git_hash(ref) which runs `git rev-parse ref`.
        # This will fail if the CWD is not a git repo containing those refs.
        # Zmake changes directory to self.module_paths["ec"] before calling CompareBuilds.
        # So we should run it with CWD set to ec_src_dir.

        executor = zmake.multiproc.Executor()

        # Mock known_modules to only include 'ec' to avoid checking other modules
        # which we don't have dummies for.
        mock_known_modules = {
            "ec": lambda name, checkout: checkout / "src" / "platform" / "ec"
        }

        with mock.patch.dict(
            "zmake.modules.known_modules", mock_known_modules, clear=True
        ):
            # We must run inside ec_src_dir so get_git_hash can find the refs
            orig_cwd = os.getcwd()
            os.chdir(ec_src_dir)
            try:
                cmp_dir = temp_dir / "compare_work"
                cmp_dir.mkdir()

                cmp_builds = zmake.compare_builds.CompareBuilds(
                    temp_dir=str(cmp_dir),
                    ref1=ref1,
                    ref2=ref2,
                    executor=executor,
                    sequential=True,
                )

                assert len(cmp_builds.checkouts) == 2
                assert cmp_builds.checkouts[0].ref == ref1
                assert cmp_builds.checkouts[1].ref == ref2

                # Perform checkouts
                module_paths = {"ec": ec_src_dir}
                cmp_builds.do_checkouts(
                    zephyr_base=zephyr_src_dir,
                    module_paths=module_paths,
                )

                # Verify checkouts content
                checkout1 = cmp_builds.checkouts[0]
                checkout2 = cmp_builds.checkouts[1]

                # Check EC files
                ec_checkout1 = checkout1.modules_dir / "ec"
                ec_checkout2 = checkout2.modules_dir / "ec"

                assert (
                    ec_checkout1 / "file1.txt"
                ).read_text() == "v1 content\n"
                assert (
                    ec_checkout2 / "file1.txt"
                ).read_text() == "v2 content\n"

                # Check Zephyr files (both should be HEAD of zephyr_src)
                zephyr_checkout1 = checkout1.zephyr_dir
                zephyr_checkout2 = checkout2.zephyr_dir

                assert (zephyr_checkout1 / "VERSION").exists()
                assert (zephyr_checkout2 / "VERSION").exists()

            finally:
                os.chdir(orig_cwd)
