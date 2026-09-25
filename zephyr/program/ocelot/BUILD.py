# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for ocelot."""


def register_npcx9_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    boards=None,
):
    """Register an npcx9 based variant of ocelot."""
    if boards is None:
        boards = ["ocelot"]

    register_npcx_project(
        project_name=project_name,
        zephyr_board="npcx9/npcx9m7f",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis_6", "ec", "pigweed", "nanopb"],
        boards=boards,
    )


def register_it8xxx2_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    boards=None,
    chip="it8xxx2/it82002aw",
):
    """Register an it8xxx2 based variant of ocelot."""
    if boards is None:
        boards = ["ocelot"]

    return register_binman_project(
        project_name=project_name,
        zephyr_board=chip,
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis_6", "ec", "pigweed", "nanopb"],
        boards=boards,
    )


def register_mec172x_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    boards=None,
):
    """Register an microchip based variant of ocelot."""
    if boards is None:
        boards = ["ocelot"]

    register_mchp_project(
        project_name=project_name,
        zephyr_board="mec172x/mec172x_nsz/mec1727",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis_6", "ec"],
        boards=boards,
    )


def register_rtk59_project(
    project_name,
    extra_kconfig_base_files=(),
    extra_kconfig_proj_files=(),
    extra_modules=(),
):
    """Register a realtek based variant of ocelot."""
    register_rtk_project(
        project_name=project_name,
        boards=["ocelot"],
        zephyr_board="realtek/rts5912",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Customization to apply before project-specific config.
            *extra_kconfig_base_files,
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_proj_files,
        ],
        modules=["cmsis_6", "ec", *extra_modules],
    )


# For use on RVP SKU1 and SKU2
register_npcx9_project(
    project_name="ocelotrvp-npcx",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
        here / "dsp_comms.conf",
    ],
)

# For use on RVP SKU3
register_it8xxx2_project(
    project_name="ocelotrvp-ite",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
        here / "dsp_comms.conf",
    ],
)

# For use on RVP SKU4
register_mec172x_project(
    project_name="ocelotrvp-mchp",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
    ],
)

register_ish_project(
    project_name="ocelotrvp-ish",
    boards=["ocelot"],
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "ocelot-ish" / "ocelotrvp-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "ocelot-ish" / "prj.conf",
        here / "ocelot-ish" / "motionsense.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
)

# For realtek
register_rtk59_project(
    project_name="ojal",
)

register_rtk59_project(
    project_name="kodkod",
)

register_rtk59_project(
    project_name="hekla",
    extra_modules=["google-private", "pigweed", "nanopb"],
)

matsu = register_it8xxx2_project(
    project_name="matsu",
)

matsu.variant(
    project_name="matsu_it82000",
    zephyr_board="it8xxx2/it82000bw",
    dts_overlays=[
        here / "matsu" / "it82000.overlay",
    ],
)


register_ish_project(
    project_name="matsu-ish",
    boards=["ocelot"],
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "matsu-ish" / "matsu-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "matsu-ish" / "prj.conf",
        here / "matsu-ish" / "motionsense.conf",
        here / ".." / ".." / "ish.conf",
    ],
)

ocicat = register_it8xxx2_project(
    project_name="ocicat",
    chip="it8xxx2/it82002bw",
    extra_kconfig_base_files=[
        here / "dsp_comms.conf",
    ],
)

register_ish_project(
    project_name="ocicat-ish",
    boards=["ocelot"],
    zephyr_board="intel_ish_5_8_0",
    dts_overlays=[
        here / "ocicat-ish" / "project.overlay",
    ],
    kconfig_files=[
        here / "ocicat-ish" / "prj.conf",
        here / "ocicat-ish" / "motionsense.conf",
        here / "dsp_comms.conf",
        here / ".." / ".." / "ish.conf",
    ],
)

register_rtk59_project(
    project_name="ocelotrvp-rtk",
    extra_kconfig_base_files=[
        here / "rvp_program.conf",
    ],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="hekla", addr=0x80404)
assert_rw_fwid_DO_NOT_EDIT(project_name="kodkod", addr=0x80404)
assert_rw_fwid_DO_NOT_EDIT(project_name="matsu", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="matsu_it82000", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-npcx", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-ite", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-mchp", addr=0x40318)
assert_rw_fwid_DO_NOT_EDIT(project_name="ojal", addr=0x80404)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocicat", addr=0x60098)
assert_rw_fwid_DO_NOT_EDIT(project_name="ocelotrvp-rtk", addr=0x80404)
