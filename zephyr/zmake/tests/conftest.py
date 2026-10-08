# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Common settings and fixtures for all tests."""

import os
import pathlib
import subprocess

# pylint: disable=import-error
import hypothesis
import pytest
import zmake.zmake as zm


hypothesis.settings.register_profile(
    "cq", suppress_health_check=list(hypothesis.HealthCheck)
)
hypothesis.settings.load_profile("cq")

# pylint: disable=redefined-outer-name,unused-argument


@pytest.fixture
def setup_git_repo():
    """Returns a helper function to initialize a git repo and make commits."""

    def _setup(repo_dir, files_contents=None):
        if files_contents is None:
            files_contents = [
                {
                    "file_to_override.c": "original content\n",
                    "file_to_patch.c": "original patch content\n",
                }
            ]
        repo_path = pathlib.Path(repo_dir)
        subprocess.run(
            ["git", "init", "--initial-branch=main"], cwd=repo_path, check=True
        )
        subprocess.run(
            ["git", "config", "user.name", "Test User"],
            cwd=repo_path,
            check=True,
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
                fpath.write_text(content, encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=repo_path, check=True)
            subprocess.run(
                ["git", "commit", "-m", f"commit {i}"],
                cwd=repo_path,
                check=True,
            )
            hash_res = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=repo_path,
                check=True,
                capture_output=True,
                text=True,
            )
            commits.append(hash_res.stdout.strip())

        return commits

    return _setup


@pytest.fixture
def zmake_factory_from_dir(tmp_path):
    """Creates module dirs and returns a Zmake object factory."""

    for module in ["ec", "pigweed", "nanopb"]:
        os.mkdir(tmp_path / module)
        os.mkdir(tmp_path / module / "zephyr")
        (tmp_path / module / "zephyr" / "module.yml").write_text("")

    zephyr_base = tmp_path / "zephyr_base"

    def _zmake_factory(**kwargs):
        return zm.Zmake(zephyr_base=zephyr_base, modules_dir=tmp_path, **kwargs)

    return _zmake_factory


@pytest.fixture
def zmake_from_dir(zmake_factory_from_dir):
    """Creates module dirs and returns a Zmake object."""
    return zmake_factory_from_dir()


@pytest.fixture
def fake_checkout(tmp_path):
    """Creates a fake checkout dir and returns the path to zmake."""

    actual_zmake_src_path = pathlib.Path(__file__).parent.parent
    fake_zmake_path = pathlib.Path(tmp_path) / "src/platform/ec/zephyr/zmake"
    os.makedirs(fake_zmake_path.parent)
    os.symlink(actual_zmake_src_path, fake_zmake_path)
    return fake_zmake_path


@pytest.fixture
def zmake_from_checkout(tmp_path, fake_checkout):
    """Creates a fake checkout dir and returns a Zmake object."""

    return zm.Zmake(checkout=tmp_path)
