<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

from zmake import signers  # pylint: disable=unused-import
import zmake.output_packers


def _register_project(**kwargs):
    kwargs.setdefault(
        "project_dir", here  # noqa: F821 pylint: disable=undefined-variable
    )
    return register_project(  # noqa: F821 pylint: disable=undefined-variable
        **kwargs
    )


#
# Note - the "ec" module must always be last in the list of modules.
# This permits the EC code to access any public APIs from the included modules.
#


def register_host_project(**kwargs):
    """Register a project that runs on a posix host."""
    kwargs.setdefault("zephyr_board", "native_sim")
    kwargs.setdefault("supported_toolchains", ["llvm", "host"])
    kwargs.setdefault("output_packer", zmake.output_packers.ElfPacker)
    return _register_project(**kwargs)


def register_raw_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.RawBinPacker)
    return _register_project(**kwargs)


# TODO: b/303828221 - Validate tokenizing with ITE
def register_binman_project(**kwargs):
    """Register a project that uses BinmanPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.BinmanPacker)
    return register_raw_project(**kwargs)


def register_npcx_project(**kwargs):
    """Register a project that uses NpcxPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.NpcxPacker)
    kwargs.setdefault("modules", ["cmsis_6", "ec"])
    return register_binman_project(**kwargs)


def register_mchp_project(**kwargs):
    """Register a project that uses MchpPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.MchpPacker)
    kwargs.setdefault("modules", ["cmsis_6", "ec"])
    return register_binman_project(**kwargs)


def register_ish_project(**kwargs):
    """Register a project that uses IshBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.IshBinPacker)
    kwargs.setdefault("modules", ["ec", "cmsis_6", "hal_intel_public"])
    return _register_project(**kwargs)


def register_rtk_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.RTKBinmanPacker)
    kwargs.setdefault("modules", ["cmsis_6", "ec"])
    return register_raw_project(**kwargs)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

from zmake import signers  # pylint: disable=unused-import
import zmake.output_packers


def _register_project(**kwargs):
    kwargs.setdefault(
        "project_dir", here  # noqa: F821 pylint: disable=undefined-variable
    )
    return register_project(  # noqa: F821 pylint: disable=undefined-variable
        **kwargs
    )


#
# Note - the "ec" module must always be last in the list of modules.
# This permits the EC code to access any public APIs from the included modules.
#


def register_host_project(**kwargs):
    """Register a project that runs on a posix host."""
    kwargs.setdefault("zephyr_board", "native_sim")
    kwargs.setdefault("supported_toolchains", ["llvm", "host"])
    kwargs.setdefault("output_packer", zmake.output_packers.ElfPacker)
    return _register_project(**kwargs)


def register_raw_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.RawBinPacker)
    return _register_project(**kwargs)


# TODO: b/303828221 - Validate tokenizing with ITE
def register_binman_project(**kwargs):
    """Register a project that uses BinmanPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.BinmanPacker)
    return register_raw_project(**kwargs)


def register_npcx_project(**kwargs):
    """Register a project that uses NpcxPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.NpcxPacker)
    kwargs.setdefault("modules", ["cmsis", "cmsis_6", "ec"])
    return register_binman_project(**kwargs)


def register_mchp_project(**kwargs):
    """Register a project that uses MchpPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.MchpPacker)
    kwargs.setdefault("modules", ["cmsis", "cmsis_6", "ec"])
    return register_binman_project(**kwargs)


def register_ish_project(**kwargs):
    """Register a project that uses IshBinPacker."""
    kwargs.setdefault("supported_toolchains", ["coreboot-sdk", "zephyr"])
    kwargs.setdefault("output_packer", zmake.output_packers.IshBinPacker)
    kwargs.setdefault("modules", ["ec", "cmsis", "cmsis_6", "hal_intel_public"])
    return _register_project(**kwargs)


def register_rtk_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.RTKBinmanPacker)
    kwargs.setdefault("modules", ["cmsis", "cmsis_6", "ec"])
    return register_raw_project(**kwargs)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
