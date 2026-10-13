# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Host tool resolution and FMAP section preservation helpers."""

import logging
import os
import shutil
import subprocess
from typing import Optional


logger = logging.getLogger(__name__)

EC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def locate_tool(
    tool_name: str, board: Optional[str] = None, ec_dir: str = EC_DIR
) -> str:
    """Locate a compiled host utility in the EC build tree or system PATH."""
    search_dirs = [os.path.join(ec_dir, "build", "host", "util")]
    if board:
        search_dirs.insert(0, os.path.join(ec_dir, "build", board, "util"))
        search_dirs.insert(
            0, os.path.join(ec_dir, "build", "zephyr", board, "util")
        )
        if os.path.exists(f"/build/{board}/usr/bin"):
            search_dirs.append(f"/build/{board}/usr/bin")

    path_env = os.pathsep.join(
        filter(None, search_dirs + [os.environ.get("PATH", "")])
    )
    found = shutil.which(tool_name, path=path_env)
    if not found:
        raise FileNotFoundError(
            f"Required flashing tool '{tool_name}' not found in PATH or "
            f"{search_dirs}. Did you build host tools?"
        )
    return found


def dump_fmap(image_path: str) -> str:
    """Execute dump_fmap -e on an image and return the output.

    Note: ChromeOS vboot_reference's dump_fmap supports -e to print extended
    human-readable rows (<name> <offset> <size> <flags>) including the
    'preserve' annotation parsed by preserve_sections_with_cur_image().
    """
    try:
        dump_fmap_bin = locate_tool("dump_fmap")
        return subprocess.check_output(
            [dump_fmap_bin, "-e", image_path], encoding="utf-8"
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as err:
        logger.warning("Failed to dump FMAP from %s: %s", image_path, err)
        return ""


def preserve_sections_with_cur_image(
    cur_img_path: str, new_img_path: str, temp_dir: str
) -> bool:
    """Extract 'preserve' sections from cur_img_path and load into new_img_path."""
    cur_img_abs = os.path.abspath(cur_img_path)
    new_img_abs = os.path.abspath(new_img_path)
    temp_dir = os.path.abspath(temp_dir)

    cur_fmap_output = dump_fmap(cur_img_abs)
    # Skip blank lines and the "name offset size flags" header.
    cur_sections = {
        parts[0]
        for parts in (line.split() for line in cur_fmap_output.splitlines())
        if parts and parts[0] != "name"
    }
    if not cur_sections:
        logger.debug("No FMAP sections found in current image.")
        return False

    new_fmap_output = dump_fmap(new_img_abs)
    preserve_sections = []
    for line in new_fmap_output.splitlines():
        parts = line.split()
        if (
            len(parts) >= 4
            and ("preserve" in parts[3].split(",") or "preserve" in parts[3:])
            and parts[0] in cur_sections
        ):
            preserve_sections.append(parts[0])

    if not preserve_sections:
        logger.debug("No FMAP sections marked as 'preserve' found.")
        return False

    logger.info("Preserving FMAP sections: %s", ", ".join(preserve_sections))

    futility_bin = locate_tool("futility")

    for section in preserve_sections:
        sec_file = os.path.join(temp_dir, f"ec.cur.{section}.bin")
        cmd_dump = [futility_bin, "dump_fmap", "-x", cur_img_abs, section]
        cmd_load = [
            futility_bin,
            "load_fmap",
            new_img_abs,
            f"{section}:{sec_file}",
        ]
        # Preservation is mandatory (like flash_ec), so say how to skip it.
        try:
            subprocess.run(cmd_dump, check=True, cwd=temp_dir)
            dumped_raw = os.path.join(temp_dir, section)
            if not os.path.exists(dumped_raw):
                raise FileNotFoundError(
                    f"Failed to extract {section} from {cur_img_abs}"
                )
            shutil.move(dumped_raw, sec_file)
            subprocess.run(cmd_load, check=True, cwd=temp_dir)
        except subprocess.CalledProcessError as err:
            raise RuntimeError(
                f"futility failed to preserve FMAP section {section}; "
                "use --no_preserve to flash without preserving it"
            ) from err

    return True
