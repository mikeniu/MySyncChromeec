# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for FPMCUs."""


def register_fpmcu_variant(
    project_name,
    zephyr_board,
    register_func,
    variant_modules=(),
    variant_optional_modules=(),
    variant_dts_overlays=(),
    variant_kconfig_files=(),
    signer=(),
    **kwargs,
):
    """Register an fpmcu variant"""
    return register_func(
        project_name=project_name,
        zephyr_board=zephyr_board,
        modules=["ec", *variant_modules],
        optional_modules=[*variant_optional_modules],
        supported_toolchains=["host/llvm", "zephyr"],
        dts_overlays=[*variant_dts_overlays],
        kconfig_files=[here / "prj.conf", *variant_kconfig_files],
        signer=signer,
        **kwargs,
    )


bloonchipper = register_fpmcu_variant(
    project_name="bloonchipper",
    zephyr_board="google_dragonclaw",
    register_func=register_binman_project,
    variant_modules=["hal_stm32", "cmsis_6"],
    variant_optional_modules=["fpc"],
    variant_dts_overlays=[
        here / "bloonchipper" / "bloonchipper.dts",
        here / "bloonchipper" / "ec_quirks.dts",
    ],
    variant_kconfig_files=[
        here / "bloonchipper" / "prj.conf",
        here / "bloonchipper" / "ec_quirks.conf",
    ],
    signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
        here / "bloonchipper" / "dev_key.pem",
    ),
    boards=[
        "brox",
        "brya",
        "fatcat",
        "guybrush",
        "rex",
        "skyrim",
        "ocelot",
    ],
)

# The address of RW_FWID is hardcoded in RO. You need to have REALLY
# good reason to change it.
assert_rw_fwid_DO_NOT_EDIT(project_name="bloonchipper", addr=0x601C8)

buccaneer = register_fpmcu_variant(
    project_name="buccaneer",
    zephyr_board="google_quincy",
    register_func=register_npcx_project,
    variant_modules=["cmsis_6"],
    variant_optional_modules=["elan_fp"],
    variant_dts_overlays=[
        here / "helipilot" / "buccaneer.dts",
    ],
    variant_kconfig_files=[
        here / "helipilot" / "prj.conf",
        here / "helipilot" / "ec_quirks.conf",
    ],
    signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
        here / "helipilot" / "buccaneer" / "dev_key.pem",
    ),
    boards=[
        "brox",
        "brya",
        "fatcat",
        "nissa",
        "rauru",
        "rex",
        "skywalker",
    ],
)

# The address of RW_FWID is hardcoded in RO. You need to have REALLY
# good reason to change it.
assert_rw_fwid_DO_NOT_EDIT(project_name="buccaneer", addr=0x40144)

helipilot = register_fpmcu_variant(
    project_name="helipilot",
    zephyr_board="google_quincy",
    register_func=register_npcx_project,
    variant_modules=["cmsis_6"],
    variant_optional_modules=["fpc"],
    variant_dts_overlays=[
        here / "helipilot" / "helipilot.dts",
    ],
    variant_kconfig_files=[
        here / "helipilot" / "prj.conf",
        here / "helipilot" / "ec_quirks.conf",
    ],
    signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
        here / "helipilot" / "dev_key.pem",
    ),
    boards=["brya", "fatcat", "rauru", "rex"],
)

# The address of RW_FWID is hardcoded in RO. You need to have REALLY
# good reason to change it.
assert_rw_fwid_DO_NOT_EDIT(project_name="helipilot", addr=0x40144)


def register_mcu_project(
    project_name,
    zephyr_board,
    mcu,
    variant_modules,
    variant_optional_modules,
    boards=(),
):
    """Register an fpmcu project based on MCU"""
    dts_path = project_name + ".dts"
    conf_path = project_name + ".conf"
    return register_fpmcu_variant(
        project_name=project_name,
        boards=boards,
        zephyr_board=zephyr_board,
        register_func=register_binman_project,
        variant_modules=variant_modules,
        variant_optional_modules=variant_optional_modules,
        variant_dts_overlays=[here / mcu / dts_path],
        variant_kconfig_files=[
            here / mcu / "prj.conf",
            here / mcu / conf_path,
        ],
        signer=signers.RwsigSigner(  # pylint: disable=undefined-variable
            here / mcu / "dev_key.pem",
        ),
    )


def register_et171_project(
    project_name,
    boards=(),
):
    """Register an fpmcu project based on ET171 MCU"""
    return register_mcu_project(
        project_name=project_name,
        zephyr_board="egis_et171",
        mcu="et171",
        variant_modules=["hal_egis", "egis_module"],
        variant_optional_modules=["egis_fp"],
        boards=boards,
    )


sanok = register_et171_project("sanok", boards=["fatcat", "atria"])
assert_rw_fwid_DO_NOT_EDIT(project_name="sanok", addr=0x42104)

srebrna = register_et171_project("srebrna", boards=["fatcat"])
assert_rw_fwid_DO_NOT_EDIT(project_name="srebrna", addr=0x42104)

stobnica = register_et171_project("stobnica", boards=["bluey"])
assert_rw_fwid_DO_NOT_EDIT(project_name="stobnica", addr=0x42104)

smolec = register_et171_project("smolec", boards=["calypso"])
assert_rw_fwid_DO_NOT_EDIT(project_name="smolec", addr=0x42104)


def register_em32f967_project(
    project_name,
    boards=(),
):
    """Register an fpmcu project based on EM32F967 MCU"""
    return register_mcu_project(
        project_name=project_name,
        zephyr_board="32f967_dv",
        mcu="em32f967",
        variant_modules=["cmsis_6", "elan_module"],
        variant_optional_modules=["elan_fp"],
        boards=boards,
    )


niedzica = register_em32f967_project("niedzica", boards=["bluey"])
assert_rw_fwid_DO_NOT_EDIT(project_name="niedzica", addr=0x24144)

nysa = register_em32f967_project("nysa", boards=[])
assert_rw_fwid_DO_NOT_EDIT(project_name="nysa", addr=0x24144)


def register_ft9001_project(
    project_name,
    boards=(),
):
    """Register an fpmcu project based on FT9001 MCU"""
    return register_mcu_project(
        project_name=project_name,
        zephyr_board="ft9001_eval",
        mcu="ft9001",
        variant_modules=["cmsis_6", "focaltech_module"],
        variant_optional_modules=["focaltech_fp"],
        boards=boards,
    )


chudow = register_ft9001_project("chudow", boards=["tanjiro"])
assert_rw_fwid_DO_NOT_EDIT(project_name="chudow", addr=0x82274)

chobienia = register_ft9001_project("chobienia", boards=["fatcat"])
assert_rw_fwid_DO_NOT_EDIT(project_name="chobienia", addr=0x82274)
