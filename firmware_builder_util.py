# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility functions for downloading CIPD packages and setting up paths."""

from dataclasses import dataclass
import os
import pathlib
import sys
from typing import List, MutableMapping, Optional


# Provide path to the root directory.
# This is needed to import chromite packages.
def setup_path() -> None:
    """Provide path to the root directory to import chromite packages."""
    path = str(pathlib.Path(__file__).resolve().parents[3])
    if path not in sys.path:
        sys.path.insert(0, path)


setup_path()

from chromite.lib import cipd  # pylint: disable=wrong-import-position


@dataclass
class PackageData:
    """Metadata for a CIPD package to install."""

    package_name: str
    package_version: str
    is_exec: bool = False
    is_pkg_config: bool = False


def is_inside_chroot() -> bool:
    """Returns True if running inside the ChromeOS SDK chroot."""
    return os.path.exists("/etc/cros_chroot_version")


def download_and_set_path(
    package_list: List[PackageData],
    env: Optional[MutableMapping[str, str]] = None,
) -> None:
    """Take list of packages to download from CIPD and update env."""
    enviroment = env if env is not None else os.environ
    cipd_bin = cipd.GetCIPDFromCache()

    for package in package_list:
        installed_path = cipd.InstallPackage(
            cipd_path=cipd_bin,
            package=package.package_name,
            version=package.package_version,
        )
        installed_path = str(installed_path)

        if package.is_exec:
            bin_dir = os.path.join(installed_path, "bin")
            target_bin = bin_dir if os.path.isdir(bin_dir) else installed_path
            current_path = enviroment.get("PATH", "")
            if target_bin not in current_path.split(os.path.pathsep):
                enviroment["PATH"] = (
                    f"{target_bin}{os.path.pathsep}{current_path}"
                )

        # Add package metadata to the PKG_CONFIG_PATH
        if package.is_pkg_config:
            pkg_dirs = [
                "lib/pkgconfig",
                "share/pkgconfig",
                "lib64/pkgconfig",
                "",
            ]
            for sub in pkg_dirs:
                candidate = (
                    os.path.join(installed_path, sub) if sub else installed_path
                )
                if os.path.isdir(candidate):
                    # If checking root dir, make sure .pc files actually exist
                    if sub == "" and not any(
                        f.endswith(".pc") for f in os.listdir(candidate)
                    ):
                        continue
                    current_pkg = enviroment.get("PKG_CONFIG_PATH", "")
                    if candidate not in current_pkg.split(os.path.pathsep):
                        enviroment["PKG_CONFIG_PATH"] = (
                            f"{candidate}{os.path.pathsep}{current_pkg}"
                            if current_pkg
                            else candidate
                        )
                    break
