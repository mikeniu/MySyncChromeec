# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module defines helpers accessible to all BUILD.py files."""

from zmake import signers  # pylint: disable=unused-import
import zmake.output_packers


def _register_project(**kwargs):
    kwargs.setdefault("project_dir", globals()["here"])
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
<<<<<<< HEAD   (d6c32f2441663510e20ae2d25d5edeb8956421b9 program,zmake: drop cmsis references)
    kwargs.setdefault("modules", ["ec", "cmsis_6", "hal_intel_public"])
||||||| BASE   (4f79cf3e850f16ecfb53fcd1c296c608faa9aef0 fpsensor: Add fingerprint_auth_enabled())
    kwargs.setdefault("modules", ["ec", "cmsis", "cmsis_6", "hal_intel_public"])
=======
    kwargs.setdefault(
        "modules",
        ["ec", "cmsis_6", "hal_intel_public", "pigweed", "nanopb"],
    )
    # TODO: remove once upstream fixes the RAM map for ISH
    kwargs["dts_overlays"].append(
        globals()["here"]
        / ".."
        / ".."
        / "include"
        / "cros"
        / "intel"
        / "ish.overlay"
    )
>>>>>>> BRANCH (98351621e96c0a9288be41db08b0e97738bbe1f1 padme: Add padme_rts5439s_vb_vif.xml)
    return _register_project(**kwargs)


def register_rtk_project(**kwargs):
    """Register a project that uses RawBinPacker."""
    kwargs.setdefault("output_packer", zmake.output_packers.RTKBinmanPacker)
    kwargs.setdefault("modules", ["cmsis_6", "ec"])
    return register_raw_project(**kwargs)
