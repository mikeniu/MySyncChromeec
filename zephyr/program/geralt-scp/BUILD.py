# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for geralt."""

register_mtkscp_project(
    project_name="geralt-scp-zephyr",
    zephyr_board="mediatek/mt81xx_scp",
    kconfig_files=[
        here / "geralt-scp-zephyr" / "project.conf",
    ],
)
