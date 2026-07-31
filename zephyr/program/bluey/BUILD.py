# Copyright 2026 The ChromiumOS Authors
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
        modules=["cmsis_6", "ec", *extra_modules],
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
    extra_modules=["google-private", "nanopb", "pigweed"],
)

register_npcx9_project(
    project_name="mica",
    zephyr_board="npcx9/npcx9m7fb",
    extra_modules=["google-private", "nanopb", "pigweed"],
)

register_npcx9_project(
    project_name="annite",
    zephyr_board="npcx9/npcx9m7fb",
)

<<<<<<< PATCH SET (6d3247d9f0af7840085a579d86e866bac4cf3bb7 bluey: Add initial Annite project)
||||||| BASE      (6de6ff8287d9bedd635f754381c03aec105d13ae fpsensor: Prevent integer overflow in get_frame buffer acces)
=======
register_npcx9_project(
    project_name="pic",
    zephyr_board="npcx9/npcx9m7fb",
)

>>>>>>> BASE      (1ae1978c83a09edd9050890cacfb96d213f9e08b bluey: add initial pic project)
# Note for reviews, do not let anyone edit these assertions, the addresses
# must not change after the first RO release.
assert_rw_fwid_DO_NOT_EDIT(project_name="bluey", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quenbi", addr=0x80144)
assert_rw_fwid_DO_NOT_EDIT(project_name="quartz", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="mica", addr=0x40144)
assert_rw_fwid_DO_NOT_EDIT(project_name="annite", addr=0x40144)
<<<<<<< PATCH SET (6d3247d9f0af7840085a579d86e866bac4cf3bb7 bluey: Add initial Annite project)
||||||| BASE      (6de6ff8287d9bedd635f754381c03aec105d13ae fpsensor: Prevent integer overflow in get_frame buffer acces)
=======
assert_rw_fwid_DO_NOT_EDIT(project_name="pic", addr=0x40144)
>>>>>>> BASE      (1ae1978c83a09edd9050890cacfb96d213f9e08b bluey: add initial pic project)
