# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for tanjiro-scp."""

register_mtkscp_project(
    project_name="sapphire-scp-zephyr",
    zephyr_board="mediatek/mt8196_scp",
    dts_overlays=[
        here / "program.dts",
        here / "sapphire-scp-zephyr" / "project.overlay",
    ],
    kconfig_files=[
        here / "program.conf",
        here / "sapphire-scp-zephyr" / "project.conf",
        here / "chre.conf",
    ],
    modules=[
        "ec",
        "chre",
        "pigweed",
    ],
    skip_build_all=not has_internal_chre(),  # pylint: disable=undefined-variable
)
