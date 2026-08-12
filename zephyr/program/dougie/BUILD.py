# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for dougie."""


def register_dougie_project(
    project_name,
    snippets=None,
):
    """Register a variant of dougie."""
    register_binman_project(
        project_name=project_name,
        zephyr_board="it8xxx2/it82002bw",
        dts_overlays=[
            here / project_name / "project.overlay",
        ],
        kconfig_files=[
            # Common to all projects.
            here / "program.conf",
            # Project-specific KConfig customization.
            here / project_name / "project.conf",
        ],
        modules=["picolibc", "ec", "pigweed"],
        inherited_from=["dougie"],
        snippets=snippets,
    )


register_dougie_project(
    project_name="bandit",
    snippets=["pw-tokenize"],
)

assert_rw_fwid_DO_NOT_EDIT(project_name="bandit", addr=0xBFFE0)
