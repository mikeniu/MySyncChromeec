# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for rauru."""

register_mtkscp_project(
    project_name="navi-scp-zephyr",
    zephyr_board="mediatek/mt8196_scp",
    dts_overlays=[here / "program.dts"],
    kconfig_files=[
        here / "program.conf",
        here / "navi-scp-zephyr" / "project.conf",
    ],
)
