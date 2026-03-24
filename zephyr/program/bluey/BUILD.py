<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for bluey."""


def register_npcx9_project(
    project_name,
    zephyr_board="npcx9/npcx9m7f",
    extra_kconfig_files=(),
    inherited_from=None,
    extra_modules=(),
):
    """Register an npcx9 based variant of bluey."""
    if inherited_from is None:
        inherited_from = ["bluey"]

    register_npcx_project(
        project_name=project_name,
        zephyr_board=zephyr_board,
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        inherited_from=inherited_from,
        modules=["cmsis", "cmsis_6", "ec", *extra_modules],
    )


register_npcx9_project(
    project_name="bluey",
)

register_npcx9_project(
    project_name="quenbi",
)

register_npcx9_project(
    project_name="quartz",
    zephyr_board="npcx9/npcx9m7fb",
    extra_modules=["google-private"],
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="bluey", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quenbi", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quartz", addr=0x40144)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for bluey."""


def register_npcx9_project(
    project_name,
    zephyr_board="npcx9/npcx9m7f",
    extra_kconfig_files=(),
    inherited_from=None,
):
    """Register an npcx9 based variant of bluey."""
    if inherited_from is None:
        inherited_from = ["bluey"]

    register_npcx_project(
        project_name=project_name,
        zephyr_board=zephyr_board,
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
            # Additional project-specific KConfig customization.
            *extra_kconfig_files,
        ],
        inherited_from=inherited_from,
    )


register_npcx9_project(
    project_name="bluey",
)

register_npcx9_project(
    project_name="quenbi",
)

register_npcx9_project(
    project_name="quartz",
    zephyr_board="npcx9/npcx9m7fb",
)

# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="bluey", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quenbi", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quartz", addr=0x40144)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
