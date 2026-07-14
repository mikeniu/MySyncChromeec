# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for herobrine."""


def register_variant(
    project_name,
):
    """Register a variant of herobrine."""
    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m3f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        inherited_from=["herobrine"],
    )


register_variant(
    project_name="evoker",
)

register_variant(
    project_name="herobrine",
)

register_variant(
    project_name="hoglin",
)

register_variant(
    project_name="villager",
)

register_variant(
    project_name="zoglin",
)

register_variant(
    project_name="zombie",
)

assert_rw_fwid_DO_NOT_EDIT(project_name="evoker", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="herobrine", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="hoglin", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="villager", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="zoglin", addr=0x7FFE0)
assert_rw_fwid_DO_NOT_EDIT(project_name="zombie", addr=0x7FFE0)
