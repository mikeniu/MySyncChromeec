#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Utility to decode Zephyr LED policy indices and LED IDs from build artifacts.

Usage:
  ./util/decode_led_policy.py <board_name>
"""

import argparse
import pathlib
import re
import sys
from typing import Dict, List, Optional, Set, Tuple


EC_DIR = pathlib.Path(__file__).resolve().parent.parent


def parse_ec_led_ids(header_path: pathlib.Path) -> Dict[int, str]:
    """Dynamically parse the enum ec_led_id definition from ec_commands.h."""
    led_mapping: Dict[int, str] = {}
    if not header_path.exists():
        return led_mapping

    content = header_path.read_text(encoding="utf-8")
    match = re.search(r"enum ec_led_id\s*\{([^}]*)\}", content)
    if not match:
        return led_mapping

    enum_body = match.group(1)
    enum_body = re.sub(r"/\*.*?\*/", "", enum_body, flags=re.DOTALL)
    enum_body = re.sub(r"//.*", "", enum_body)

    current_idx = 0
    for line in enum_body.split(","):
        stripped = line.strip()
        if not stripped:
            continue

        if "=" in stripped:
            name, val = stripped.split("=")
            name = name.strip()
            current_idx = int(val.strip(), 0)
        else:
            name = stripped

        if name != "EC_LED_ID_COUNT":
            led_mapping[current_idx] = name
            current_idx += 1

    return led_mapping


def extract_used_leds(dts_lines: List[str]) -> Set[str]:
    """Extract all LED ID strings referenced in the devicetree."""
    used_leds: Set[str] = set()
    for line in dts_lines:
        if "led-id =" in line:
            parts = line.split('"')
            if len(parts) >= 3:
                used_leds.add(parts[1])
    return used_leds


def parse_led_policies(dts_lines: List[str]) -> List[Tuple[str, List[str]]]:
    """Parse LED policy groups and their individual policy nodes in order."""
    policy_groups: List[Tuple[str, List[str]]] = []
    in_policy = False
    brace_level = 0
    current_group_name = ""
    current_policies: List[str] = []

    for i, line in enumerate(dts_lines):
        if 'compatible = "cros-ec,led-policy";' in line:
            if current_group_name and current_policies:
                policy_groups.append((current_group_name, current_policies))
            in_policy = True
            brace_level = 1
            current_policies = []

            group_name_idx = i - 1
            while group_name_idx > 0 and "{" not in dts_lines[group_name_idx]:
                group_name_idx -= 1
            if group_name_idx > 0:
                current_group_name = (
                    dts_lines[group_name_idx].split("{")[0].strip()
                )
            else:
                current_group_name = "unknown-group"
            continue

        if in_policy:
            if "{" in line:
                brace_level += line.count("{")
                if brace_level == 2:
                    node_name = line.split("{")[0].strip()
                    if not node_name:
                        node_name = dts_lines[i - 1].strip()
                    current_policies.append(node_name)

            if "}" in line:
                brace_level -= line.count("}")
                if brace_level <= 0:
                    in_policy = False
                    if current_group_name:
                        policy_groups.append(
                            (current_group_name, current_policies)
                        )
                        current_group_name = ""
                        current_policies = []

    return policy_groups


def find_dts_file(
    board: str, build_dir: Optional[pathlib.Path] = None
) -> Optional[pathlib.Path]:
    """Locate the compiled zephyr.dts file for the given board."""
    base = build_dir or (EC_DIR / "build" / "zephyr" / board)
    candidates = [
        base / "build-rw" / "zephyr" / "zephyr.dts",
        base / "build-ro" / "zephyr" / "zephyr.dts",
        base / "zephyr" / "zephyr.dts",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def main(argv: Optional[List[str]] = None) -> int:
    """Main entry point to parse and print LED policy mapping."""
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "board",
        help="Board name (e.g. geralt, skyrim, quartz)",
    )
    parser.add_argument(
        "--dts",
        type=pathlib.Path,
        help="Explicit path to zephyr.dts (overrides board auto-detection)",
    )
    parser.add_argument(
        "--header",
        type=pathlib.Path,
        default=EC_DIR / "include" / "ec_commands.h",
        help="Path to include/ec_commands.h (default: %(default)s)",
    )

    args = parser.parse_args(argv)

    dts_path = args.dts or find_dts_file(args.board)
    if not dts_path or not dts_path.is_file():
        print(
            f"Error: Could not find DTS file for board '{args.board}'.\n"
            f"Please ensure you have built the image with `zmake build {args.board}`.",
            file=sys.stderr,
        )
        return 1

    dts_lines = dts_path.read_text(encoding="utf-8").splitlines()

    # 1. Parse and print LED IDs
    led_map = parse_ec_led_ids(args.header)
    used_leds = extract_used_leds(dts_lines)

    print(f"--- LED ID Mapping (Used by {args.board}) ---")
    found_used = False
    for idx in sorted(led_map.keys()):
        if led_map[idx] in used_leds:
            print(f"led {idx:<6} ->  {led_map[idx]}")
            found_used = True

    if not found_used:
        print("No LEDs defined in this devicetree.")
    print("")

    # 2. Parse and print Policy Groups
    policy_groups = parse_led_policies(dts_lines)
    print(f"--- LED Policy Mapping for '{args.board}' ---")
    if not policy_groups:
        print("No policies found in this devicetree.")
    else:
        for group_name, policies in policy_groups:
            print(f"\n[Policy Group: {group_name}]")
            for idx, policy in enumerate(policies):
                print(f"Policy {idx:<2} ->  {policy}")
        print("")

    return 0


if __name__ == "__main__":
    sys.exit(main())
