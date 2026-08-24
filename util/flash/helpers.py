import logging
import subprocess
import os

logger = logging.getLogger(__name__)

def dump_fmap(image_path, flag="-e"):
    """Dumps the flash map of an image using dump_fmap."""
    try:
        content = subprocess.check_output(
            ["dump_fmap", flag, image_path], encoding="utf-8"
        )
        return content
    except subprocess.CalledProcessError as e:
        logger.warning(f"dump_fmap failed on {image_path}: {e}")
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
            logger.info(f"Preserving {section_name} section on EC chip")
            
            sec_img = os.path.join(temp_dir, f"ec.cur.{section_name}.bin")
            
            try:
                subprocess.run(["futility", "dump_fmap", "-x", cur_img, section_name], check=True, cwd=temp_dir)
                subprocess.run(["mv", section_name, sec_img], check=True, cwd=temp_dir)
                subprocess.run(["futility", "load_fmap", new_img, f"{section_name}:{sec_img}"], check=True, cwd=temp_dir)
            except subprocess.CalledProcessError as e:
                logger.error(f"Failed to preserve {section_name}: {e}")
                raise
    return True

import shutil

def locate_tool(tool_name):
    """
    Locates a tool in the system PATH.
    If not found, attempts to build it using the EC util Makefile recovery logic.
    """
    path = shutil.which(tool_name)
    if path:
        return path

    logger.info(f"Tool {tool_name} not found in PATH, attempting to build it.")
    
    script_dir = os.path.dirname(os.path.realpath(__file__))
    ec_dir = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))

    # The bash script logic and ChromiumOS EC Makefile uses BOARD=host utils-host
    try:
        subprocess.run(["make", "-j", "-C", ec_dir, "BOARD=host", "utils-host"], check=True)
    except subprocess.CalledProcessError as e:
        logger.warning(f"Failed to build tools: {e}")
            
    # Check again if it was built and can be found, possibly by looking in the build dir
    build_util_path = os.path.join(ec_dir, "build", "host", "util", tool_name)
    if os.path.exists(build_util_path):
        return build_util_path
    
    # Check current tree's util dir
    root_util_path = os.path.join(ec_dir, "util", tool_name)
    if os.path.exists(root_util_path):
        return root_util_path
        
    logger.error(f"Cannot find or build {tool_name}")
    raise RuntimeError(f"Missing dependency: {tool_name}")
