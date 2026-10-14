# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Host tool resolution and FMAP section preservation helpers."""

from dataclasses import dataclass
import logging
import os
import shutil
import struct
import subprocess
from typing import Dict, List, Optional, Sequence, Tuple


logger = logging.getLogger(__name__)

EC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

FMAP_SIGNATURE = b"__FMAP__"
FMAP_HEADER_FORMAT = "<8sBBQI32sH"
FMAP_HEADER_SIZE = struct.calcsize(FMAP_HEADER_FORMAT)
FMAP_AREA_FORMAT = "<II32sH"
FMAP_AREA_SIZE = struct.calcsize(FMAP_AREA_FORMAT)
FMAP_AREA_PRESERVE = 0x08


@dataclass(frozen=True)
class FmapArea:
    """Represents a single FMAP area parsed from an EC firmware image."""

    name: str
    offset: int
    size: int
    flags: int = 0

    @property
    def is_preserve(self) -> bool:
        """Return True if this section should not be overwritten by default."""
        return self.name == "CBI" or bool(self.flags & FMAP_AREA_PRESERVE)


def parse_fmap_bytes(data: bytes) -> Dict[str, FmapArea]:
    """Parse the binary __FMAP__ table directly from firmware image bytes."""
    search_pos = 0
    while True:
        idx = data.find(FMAP_SIGNATURE, search_pos)
        if idx < 0 or idx + FMAP_HEADER_SIZE > len(data):
            return {}
        search_pos = idx + 1

        (
            _sig,
            ver_major,
            _ver_minor,
            _base,
            fmap_size,
            _name,
            nareas,
        ) = struct.unpack_from(FMAP_HEADER_FORMAT, data, idx)
        if ver_major != 1 or fmap_size == 0 or nareas == 0 or nareas > 128:
            continue
        table_end = idx + FMAP_HEADER_SIZE + nareas * FMAP_AREA_SIZE
        if table_end > len(data):
            continue

        max_extent = max(len(data), fmap_size)
        areas: Dict[str, FmapArea] = {}
        valid = True
        for i in range(nareas):
            entry_off = idx + FMAP_HEADER_SIZE + i * FMAP_AREA_SIZE
            area_offset, area_size, raw_name, area_flags = struct.unpack_from(
                FMAP_AREA_FORMAT, data, entry_off
            )
            name_bytes = raw_name.split(b"\x00", 1)[0]
            try:
                area_name = name_bytes.decode("ascii")
            except UnicodeDecodeError:
                valid = False
                break
            if not area_name or area_offset + area_size > max_extent:
                valid = False
                break
            areas[area_name] = FmapArea(
                name=area_name,
                offset=area_offset,
                size=area_size,
                flags=area_flags,
            )
        if valid and areas:
            return areas


def parse_fmap_areas(image_path: str) -> Dict[str, FmapArea]:
    """Parse FMAP areas from an image via binary inspection or dump_fmap."""
    if image_path:
        try:
            with open(image_path, "rb") as img_f:
                data = img_f.read()
            if FMAP_SIGNATURE in data:
                areas = parse_fmap_bytes(data)
                if areas:
                    return areas
        except OSError:
            pass

    fmap_out = dump_fmap(image_path)
    areas = {}
    for line in fmap_out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            name = parts[0]
            offset = int(parts[1], 0)
            size = int(parts[2], 0)
        except ValueError:
            continue
        flags = 0
        if len(parts) >= 4 and (
            "preserve" in parts[3].split(",") or "preserve" in parts[3:]
        ):
            flags |= FMAP_AREA_PRESERVE
        areas[name] = FmapArea(name=name, offset=offset, size=size, flags=flags)
    return areas


def _merge_ranges(ranges: Sequence[Tuple[int, int]]) -> List[Tuple[int, int]]:
    """Sort and merge overlapping or adjacent [start, end) byte ranges."""
    valid = sorted((s, e) for s, e in ranges if e > s)
    if not valid:
        return []
    merged = [valid[0]]
    for start, end in valid[1:]:
        prev_start, prev_end = merged[-1]
        if start <= prev_end:
            merged[-1] = (prev_start, max(prev_end, end))
        else:
            merged.append((start, end))
    return merged


def subtract_ranges(
    base_ranges: Sequence[Tuple[int, int]],
    excluded_ranges: Sequence[Tuple[int, int]],
) -> List[Tuple[int, int]]:
    """Subtract excluded [start, end) ranges from base_ranges."""
    result = _merge_ranges(base_ranges)
    for ex_start, ex_end in _merge_ranges(excluded_ranges):
        updated: List[Tuple[int, int]] = []
        for start, end in result:
            if ex_end <= start or ex_start >= end:
                updated.append((start, end))
            else:
                if start < ex_start:
                    updated.append((start, ex_start))
                if ex_end < end:
                    updated.append((ex_end, end))
        result = updated
    return result


def get_preserve_ranges(
    image_path: str,
    areas: Optional[Dict[str, FmapArea]] = None,
) -> List[Tuple[int, int]]:
    """Return sorted [start, end) ranges for CBI and preserve FMAP areas."""
    if areas is None:
        areas = parse_fmap_areas(image_path)
    return _merge_ranges(
        [
            (area.offset, area.offset + area.size)
            for area in areas.values()
            if area.is_preserve and area.size > 0
        ]
    )


def get_writable_ranges(
    image_path: str,
    total_size: int,
    target_regions: Optional[Sequence[str]] = None,
    no_preserve: bool = False,
    areas: Optional[Dict[str, FmapArea]] = None,
) -> List[Tuple[int, int]]:
    """Return [start, end) byte ranges to write after excluding CBI/preserve.

    Raises:
        ValueError: A requested target region is not in the image FMAP.
    """
    if areas is None:
        areas = parse_fmap_areas(image_path)
    base_ranges: List[Tuple[int, int]] = []

    for reg_name in target_regions or ():
        area = areas.get(reg_name)
        if area is None and reg_name == "EC_RO":
            area = areas.get("WP_RO")
        if area is None or area.size <= 0 or area.offset >= total_size:
            raise ValueError(f"Image FMAP has no {reg_name} area to write")
        base_ranges.append(
            (area.offset, min(total_size, area.offset + area.size))
        )

    if not base_ranges:
        base_ranges = [(0, total_size)]
    else:
        base_ranges = _merge_ranges(base_ranges)

    if no_preserve or not areas:
        return base_ranges

    excluded = [
        (area.offset, area.offset + area.size)
        for area in areas.values()
        if area.is_preserve and area.size > 0
    ]
    return subtract_ranges(base_ranges, excluded)


def resolve_image_writable_ranges(
    image_path: str,
    ro_only: bool = False,
    rw_only: bool = False,
    no_preserve: bool = False,
) -> Tuple[Dict[str, FmapArea], int, List[Tuple[int, int]]]:
    """Parse FMAP areas and compute non-preserved writable byte ranges."""
    if ro_only and rw_only:
        raise ValueError("Cannot specify both ro_only and rw_only")
    target_regions = None
    if ro_only:
        target_regions = ["EC_RO"]
    elif rw_only:
        target_regions = ["EC_RW"]
    areas = (
        parse_fmap_areas(image_path)
        if (target_regions or not no_preserve)
        else {}
    )
    if not areas and not target_regions:
        return {}, 0, []
    try:
        img_size = os.path.getsize(image_path)
    except OSError:
        img_size = max((a.offset + a.size for a in areas.values()), default=0)
    writable_ranges = get_writable_ranges(
        image_path,
        img_size,
        target_regions=target_regions,
        no_preserve=no_preserve,
        areas=areas,
    )
    return areas, img_size, writable_ranges


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
