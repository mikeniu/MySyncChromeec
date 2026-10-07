# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for rauru."""

register_mtkscp_project(
    project_name="navi-scp-zephyr",
    zephyr_board="mediatek/mt8196_scp",
    dts_overlays=[
        here / "program.dts",
        here / "navi-scp-zephyr" / "project.overlay",
    ],
    kconfig_files=[
        here / "program.conf",
        here / "navi-scp-zephyr" / "project.conf",
    ],
)

register_mtkscp_project(
    project_name="hylia-scp-zephyr",
    zephyr_board="mediatek/mt8196_scp",
    dts_overlays=[
        here / "program.dts",
        here / "hylia-scp-zephyr" / "project.overlay",
    ],
    kconfig_files=[
        here / "program.conf",
        here / "hylia-scp-zephyr" / "project.conf",
    ],
)
