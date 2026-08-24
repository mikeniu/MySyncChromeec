#!/usr/bin/env vpython3

"""Helper functions for EC flash operations."""

# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import os
import shutil
import subprocess


logger = logging.getLogger(__name__)

EC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def dump_fmap(image_path, flag="-e"):
    """Dumps the flash map of an image using dump_fmap."""
    try:
        dump_fmap_path = locate_tool("dump_fmap")
        content = subprocess.check_output(
            [dump_fmap_path, flag, image_path], encoding="utf-8"
        )
        return content
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        logger.warning("dump_fmap failed on %s: %s", image_path, e)
        return ""


def preserve_sections_with_cur_image(cur_img, new_img, temp_dir):
    """
    Looks for preserved FMAP sections in cur_img, extracts them,
    and injects them into new_img using futility load_fmap.
    """
    fmap_output = dump_fmap(cur_img, "-e")
    if not fmap_output or " preserve" not in fmap_output:
        logger.info("No sections are being preserved")
        return False

    for line in fmap_output.splitlines():
        if " preserve" in line:
            parts = line.split()
            section_name = parts[0]
            logger.info("Preserving %s section on EC chip", section_name)

            sec_img = os.path.join(temp_dir, f"ec.cur.{section_name}.bin")

            try:
                subprocess.run(
                    ["futility", "dump_fmap", "-x", cur_img, section_name],
                    check=True,
                    cwd=temp_dir,
                )
                shutil.move(os.path.join(temp_dir, section_name), sec_img)
                subprocess.run(
                    [
                        "futility",
                        "load_fmap",
                        new_img,
                        f"{section_name}:{sec_img}",
                    ],
                    check=True,
                    cwd=temp_dir,
                )
            except subprocess.CalledProcessError as e:
                logger.error("Failed to preserve %s: %s", section_name, e)
                raise
    return True


def locate_tool(tool_name):
    """
    Locates a tool in the system PATH.
    If not found, attempts to build it using the EC util Makefile recovery logic.
    """
    path = shutil.which(tool_name)
    if path:
        return path

    logger.info("Tool %s not found in PATH, attempting to build it.", tool_name)

    ec_dir = EC_DIR

    # The bash script logic and ChromiumOS EC Makefile uses BOARD=host utils-host
    try:
        subprocess.run(
            ["make", "-j", "-C", ec_dir, "BOARD=host", "utils-host"], check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        logger.warning("Failed to build tools: %s", e)

    # Check again if it was built and can be found, possibly by looking in the build dir
    build_util_path = os.path.join(ec_dir, "build", "host", "util", tool_name)
    if os.path.exists(build_util_path):
        return build_util_path

    # Check current tree's util dir
    root_util_path = os.path.join(ec_dir, "util", tool_name)
    if os.path.exists(root_util_path):
        return root_util_path

    logger.error("Cannot find or build %s", tool_name)
    raise RuntimeError(f"Missing dependency: {tool_name}")
