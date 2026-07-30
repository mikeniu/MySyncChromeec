#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Zephyr EC Kconfig Size Optimizer and Analysis Tool.

This utility analyzes Flash (ROM) and RAM (SRAM) usage in Zephyr EC builds by
evaluating Kconfig options. It tests disabling, enabling, toggling, or adjusting
various Kconfigs to measure their exact size impact and side effects, using
dependency graph analysis with kconfiglib and fast incremental ninja builds.
"""

# pylint: disable=too-many-lines

import argparse
import concurrent.futures
import json
import logging
import os
import pathlib
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from typing import Any, Dict, List, Optional, Tuple


# Locate EC_BASE and ZEPHYR_BASE
def find_ec_base() -> pathlib.Path:
    """Locate the platform/ec base directory."""
    candidates = [
        pathlib.Path(__file__).resolve().parent.parent,
        pathlib.Path.cwd().resolve(),
    ]
    for candidate in candidates:
        if (candidate / "zephyr" / "zmake").exists():
            return candidate
    return candidates[0]


EC_BASE = find_ec_base()
if "ZEPHYR_BASE" in os.environ:
    ZEPHYR_BASE = pathlib.Path(os.environ["ZEPHYR_BASE"]).resolve()
else:
    ZEPHYR_BASE = (
        EC_BASE.parent.parent / "third_party" / "zephyrproject" / "zephyr"
    ).resolve()

# Add Zephyr kconfiglib path to sys.path
sys.path.insert(0, str(ZEPHYR_BASE / "scripts" / "kconfig"))
sys.path.insert(0, str(ZEPHYR_BASE / "scripts"))

import kconfiglib  # pylint: disable=import-error,wrong-import-position


# ==============================================================================
# Known High-Impact & Optional Kconfigs Database
# ==============================================================================

# Optional Memory and Stack Tuning Kconfigs (Requires --include-memory-options)
MEMORY_TUNING_KCONFIGS: Dict[str, List[str]] = {
    "MAIN_STACK_SIZE": ["1024", "2048"],
    "ISR_STACK_SIZE": ["1024", "1536"],
    "SYSTEM_WORKQUEUE_STACK_SIZE": ["1024", "1536"],
    "HEAP_MEM_POOL_SIZE": ["0", "256", "512"],
    "IDLE_STACK_SIZE": ["256", "384"],
    "LOG_BUFFER_SIZE": ["512", "1024"],
    "SHELL_BACKEND_RTT_BUFFER_SIZE": ["256"],
    "SHELL_CMD_BUFF_SIZE": ["64", "128"],
}

# Patterns for Mandatory Kconfigs that MUST NOT be disabled or tested
MANDATORY_KCONFIG_PATTERNS = [
    r"^ARCH$",
    r"^BOARD_",
    r"^SOC$",
    r"^SOC_",
    r"^DT_HAS_",
    r"^CPU$",
    r"^ARM$",
    r"^CORTEX",
    r"^MULTITHREADING$",
    r"^NUM_IRQS$",
    r"^SYS_CLOCK_EXISTS$",
    r"^GENERIC_INTERRUPTS$",
    r"^SW_ISR_TABLE$",
    r"^ISR_TABLES$",
    r"^GEN_ISR_TABLES$",
    r"^GEN_IRQ_VECTOR_TABLE$",
    r"^BUILD_OUTPUT_",
    r"^XIP$",
    r"^HAS_DTS$",
    # Exact-match top-level module switches only (individual PLATFORM_EC_*
    # and CROS_EC_* feature options do not match '$' and are still tested).
    r"^PLATFORM_EC$",
    r"^CROS_EC$",
    r"^SHIM$",
    r"^ZEPHYR_BASE$",
    r"^IS_32BIT$",
    r"^LITTLE_ENDIAN$",
    r"^LINKER_",
    r"^TOOLCHAIN_",
    r"^OUTPUT_PRINT_MEMORY_USAGE$",
]


# ==============================================================================
# Helper & Utility Functions
# ==============================================================================


def is_log_kconfig(sym_name: str) -> bool:
    """Check if a Kconfig symbol is log or debug-level related."""
    upper = sym_name.upper()
    if upper in [
        "LOG",
        "PRINTK",
        "BOOT_BANNER",
        "ASSERT",
        "ASSERT_LEVEL",
        "LOG_DEFAULT_LEVEL",
        "LOG_MAX_LEVEL",
        "LOG_BUFFER_SIZE",
        "LOG_PRINTK",
        "TRACING",
    ]:
        return True
    if any(
        upper.endswith(suffix)
        for suffix in [
            "_LOG_LEVEL",
            "_DEBUG_LEVEL",
            "_ASSERT_LEVEL",
            "_LOG_LEVEL_OFF",
            "_LOG_LEVEL_ERR",
            "_LOG_LEVEL_WRN",
            "_LOG_LEVEL_INF",
            "_LOG_LEVEL_DBG",
        ]
    ):
        return True
    if (
        "LOG_LEVEL" in upper
        or "DEBUG_LEVEL" in upper
        or upper.startswith("LOG_")
    ):
        return True
    return False


def has_prompt(sym) -> bool:
    """Check if any definition node of a Kconfig symbol has a user prompt."""
    return bool(sym.nodes and any(n.prompt for n in sym.nodes))


def is_mandatory_kconfig(sym_name: str, kconf=None) -> bool:
    """Check if a Kconfig symbol is mandatory or non-configurable."""
    for pattern in MANDATORY_KCONFIG_PATTERNS:
        if re.search(pattern, sym_name):
            return True
    if kconf and sym_name in kconf.syms:
        sym = kconf.syms[sym_name]
        if not has_prompt(sym):
            return True
    return False


def parse_pre_ilm_flash_padding(map_path: Optional[pathlib.Path]) -> int:
    """Parse pre-ILM 4KB alignment padding before __ilm_flash_start in map."""
    if not map_path or not map_path.exists():
        return 0
    try:
        content = map_path.read_text(encoding="utf-8", errors="ignore")
        m = re.search(
            r"\*fill\*\s+0x[0-9a-fA-F]+\s+(0x[0-9a-fA-F]+)\s*\n"
            r"\s*0x[0-9a-fA-F]+\s+__ilm_flash_start\s*=\s*\.",
            content,
        )
        if m:
            return int(m.group(1), 16)
    except Exception:  # pylint: disable=broad-exception-caught
        pass
    return 0


def parse_memory_usage(
    output: str, map_path: Optional[pathlib.Path] = None
) -> Tuple[int, int]:
    """Parse Flash/ROM and RAM size in bytes from Ninja/LD build output."""
    flash_unpadded = 0
    flash_generic = 0
    ram_unpadded = 0
    ram_generic = 0

    for line in output.splitlines():
        line = line.strip()
        # Match lines like: FLASH_RO: 278060 B, SRAM: 41852 B,
        # RW_ROM_UNPADDED: 331740 B, or RAM: 57740 B
        m = re.search(
            r"\b([A-Z_]*(?:FLASH|ROM|RAM|SRAM)[A-Z_]*):\s*(\d+)\s*([KMGT]?B)",
            line,
            re.IGNORECASE,
        )
        if m:
            region, size, unit = (
                m.group(1).upper(),
                int(m.group(2)),
                m.group(3).upper(),
            )
            bytes_val = size
            if unit == "KB":
                bytes_val *= 1024
            elif unit == "MB":
                bytes_val *= 1024 * 1024
            elif unit == "GB":
                bytes_val *= 1024 * 1024 * 1024

            if "FLASH" in region or "ROM" in region:
                if "UNPADDED" in region:
                    if flash_unpadded == 0:
                        flash_unpadded = bytes_val
                else:
                    if flash_generic == 0:
                        flash_generic = bytes_val
            elif "RAM" in region or "SRAM" in region:
                if "UNPADDED" in region:
                    if ram_unpadded == 0:
                        ram_unpadded = bytes_val
                else:
                    if ram_generic == 0:
                        ram_generic = bytes_val

    flash_bytes = flash_unpadded if flash_unpadded > 0 else flash_generic
    if flash_bytes > 0:
        flash_bytes -= parse_pre_ilm_flash_padding(map_path)
    ram_bytes = ram_unpadded if ram_unpadded > 0 else ram_generic

    return flash_bytes, ram_bytes


# pylint: disable=too-many-return-statements
def extract_failure_reason(error_log: str) -> str:
    """Extract a human-readable failure reason from build/Kconfig error log."""
    if not error_log:
        return "Build compilation failed"
    m_sel = re.search(
        r"is currently being y-selected by the following symbols:"
        r"\s*\n\s*-\s*([A-Z0-9_]+)",
        error_log,
    )
    if m_sel:
        return f"Selected by active symbol CONFIG_{m_sel.group(1)}"
    m_dep = re.search(
        r"warning:\s+([A-Z0-9_]+).*?has direct dependencies (.*?) with value n",
        error_log,
        re.DOTALL,
    )
    if m_dep:
        dep_str = m_dep.group(2).strip().replace("\n", " ")
        return f"Unsatisfied dependency: {m_dep.group(1)} depends on {dep_str}"
    m_undef = re.search(
        r"attempt to assign the value '.*?' to the undefined symbol "
        r"([A-Z0-9_]+)",
        error_log,
    )
    if m_undef:
        return f"Undefined symbol CONFIG_{m_undef.group(1)}"
    m_ref = re.search(r"undefined reference to [`']([a-z0-9_]+)", error_log)
    if m_ref:
        return f"Linker error: undefined reference to {m_ref.group(1)}"
    m_overflow = re.search(
        r"region [`']([A-Za-z0-9_]+)['`]\s+overflowed by (\d+\s+bytes)",
        error_log,
        re.IGNORECASE,
    )
    if m_overflow:
        return (
            f"Linker error: region '{m_overflow.group(1)}' "
            f"overflowed by {m_overflow.group(2)}"
        )
    m_assert = re.search(r"AssertionError:\s+([^\n]+)", error_log)
    if m_assert:
        return f"RAM allocation error: {m_assert.group(1).strip()}"
    m_gcc = re.search(r"error:\s+([^\n]+)", error_log, re.IGNORECASE)
    if m_gcc and "ld returned 1 exit status" not in m_gcc.group(1).lower():
        return f"Compiler error: {m_gcc.group(1).strip()}"
    return "Build compilation failed"


# ==============================================================================
# Dependency & Side-Effect Resolver Class
# ==============================================================================


class KconfigDependencyResolver:
    """Manages Kconfig tree parsing and dependency resolution via kconfiglib."""

    def __init__(
        self,
        zephyr_base: pathlib.Path,
        ec_base: pathlib.Path,
        build_dir: pathlib.Path,
    ):
        self.zephyr_base = zephyr_base
        self.ec_base = ec_base
        self.build_dir = build_dir
        self.kconf = None
        self._lock = threading.RLock()
        self._load_kconfig_tree()

    def _load_kconfig_tree(self):
        """Initialize kconfiglib with the build environment and load .config."""

        build_kconfig_dir = self.build_dir / "Kconfig"
        if (
            not build_kconfig_dir.exists()
            and (self.build_dir / "build-rw" / "Kconfig").exists()
        ):
            build_kconfig_dir = self.build_dir / "build-rw" / "Kconfig"

        edt_pickle = self.build_dir / "zephyr" / "edt.pickle"
        if (
            not edt_pickle.exists()
            and (self.build_dir / "build-rw" / "zephyr" / "edt.pickle").exists()
        ):
            edt_pickle = self.build_dir / "build-rw" / "zephyr" / "edt.pickle"
        if edt_pickle.exists():
            os.environ["EDT_PICKLE"] = str(edt_pickle)
            if "kconfigfunctions" in sys.modules:
                import importlib  # pylint: disable=import-outside-toplevel

                importlib.reload(sys.modules["kconfigfunctions"])

        sys.path.insert(0, str(self.zephyr_base / "scripts" / "kconfig"))

        os.environ["KCONFIG_BINARY_DIR"] = str(build_kconfig_dir)
        os.environ["KCONFIG_BOARD_DIR"] = str(build_kconfig_dir / "boards")
        os.environ["ZEPHYR_BASE"] = str(self.zephyr_base)
        os.environ["srctree"] = str(self.zephyr_base)
        os.environ["ARCH"] = "*"
        os.environ["ARCH_DIR"] = str(self.zephyr_base / "arch")
        os.environ["HWM_SCHEME"] = "v2"
        os.environ["BOARD_DIR"] = str(
            self.zephyr_base / "boards" / "arm" / "null"
        )
        os.environ["TOOLCHAIN_KCONFIG_DIR"] = str(
            self.zephyr_base / "cmake" / "toolchain" / "coreboot-sdk"
        )
        os.environ["TOOLCHAIN_HAS_NEWLIB"] = "y"
        os.environ["TOOLCHAIN_HAS_PICOLIBC"] = "y"
        os.environ["ZEPHYR_CMSIS_6_KCONFIG"] = "/dev/null"
        os.environ["ZEPHYR_EC_KCONFIG"] = str(self.ec_base / "Kconfig")

        app_dir = None
        cache_file = self.build_dir / "CMakeCache.txt"
        if (
            not cache_file.exists()
            and (self.build_dir / "build-rw" / "CMakeCache.txt").exists()
        ):
            cache_file = self.build_dir / "build-rw" / "CMakeCache.txt"
        if cache_file.exists():
            with open(cache_file, "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("APPLICATION_SOURCE_DIR:PATH="):
                        app_dir = pathlib.Path(line.strip().split("=", 1)[1])
                        os.environ["APPLICATION_SOURCE_DIR"] = str(app_dir)
                        os.environ["APP_DIR"] = str(app_dir)
                    elif line.startswith("BOARD_DIR:PATH="):
                        os.environ["BOARD_DIR"] = line.strip().split("=", 1)[1]

        env_file = build_kconfig_dir / "kconfig_module_dirs.env"
        if env_file.exists():
            with open(env_file, "r", encoding="utf-8") as f:
                for line in f:
                    if "=" in line:
                        k, v = line.strip().split("=", 1)
                        os.environ[k] = v

        try:
            root_kconfig = self.zephyr_base / "Kconfig"
            if app_dir and (app_dir / "Kconfig").exists():
                root_kconfig = app_dir / "Kconfig"
            if not root_kconfig.exists():
                logging.warning(
                    "Root Kconfig file not found at %s", root_kconfig
                )
                return
            self.kconf = kconfiglib.Kconfig(
                str(root_kconfig), warn_to_stderr=False
            )
            config_file = self.build_dir / "zephyr" / ".config"
            if (
                not config_file.exists()
                and (
                    self.build_dir / "build-rw" / "zephyr" / ".config"
                ).exists()
            ):
                config_file = self.build_dir / "build-rw" / "zephyr" / ".config"
            if config_file.exists():
                self.kconf.load_config(str(config_file))
                logging.info(
                    "Loaded .config into kconfiglib (%d symbols)",
                    len(self.kconf.syms),
                )
        except Exception as err:  # pylint: disable=broad-exception-caught
            logging.warning(
                "Could not load Kconfig tree with kconfiglib: %s", err
            )
            self.kconf = None

    def has_symbol(self, sym_name: str) -> bool:
        """Thread-safely check whether sym_name exists in the Kconfig tree."""
        with self._lock:
            return bool(self.kconf and sym_name in self.kconf.syms)

    def get_symbol_value(
        self, sym_name: str, default: Optional[str] = None
    ) -> Optional[str]:
        """Thread-safely return the string value of sym_name."""
        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return default
            return self.kconf.syms[sym_name].str_value

    def is_bool_or_tristate(self, sym_name: str, default: bool = True) -> bool:
        """Thread-safely check whether sym_name is BOOL or TRISTATE."""
        with self._lock:
            if (
                not self.kconf
                or sym_name not in self.kconf.syms
                or kconfiglib is None
            ):
                return default
            return self.kconf.syms[sym_name].type in (
                kconfiglib.BOOL,
                kconfiglib.TRISTATE,
            )

    def is_mandatory_symbol(self, sym_name: str) -> bool:
        """Thread-safely check whether sym_name is mandatory/non-prompt."""
        with self._lock:
            return is_mandatory_kconfig(sym_name, self.kconf)

    def get_side_effects(
        self, sym_name: str, new_val: str = "n"
    ) -> List[Tuple[str, str, str]]:
        """Calculate side effects (other Kconfigs changing) for sym_name."""
        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return []

            orig_state = {
                name: sym.str_value for name, sym in self.kconf.syms.items()
            }

            target_sym = self.kconf.syms[sym_name]
            orig_target_val = target_sym.str_value
            orig_choice_sel = (
                target_sym.choice.selection if target_sym.choice else None
            )
            changes = []
            try:
                target_sym.set_value(new_val)
                for name, orig_val in orig_state.items():
                    if name == sym_name:
                        continue
                    curr_val = self.kconf.syms[name].str_value
                    if curr_val != orig_val:
                        changes.append((name, orig_val, curr_val))
            except Exception:  # pylint: disable=broad-exception-caught
                return []
            finally:
                try:
                    if orig_choice_sel is not None:
                        orig_choice_sel.set_value("y")
                    else:
                        target_sym.set_value(orig_target_val)
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

            return changes

    def is_selected_by_others(self, sym_name: str) -> List[str]:
        """Check if sym_name is selected by other active options."""
        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return []
            sym = self.kconf.syms[sym_name]
            if not hasattr(sym, "rev_dep") or not sym.rev_dep:
                return []
            try:
                return [
                    x.name
                    for x in kconfiglib.expr_items(sym.rev_dep)
                    if x.name in self.kconf.syms
                    and self.kconf.syms[x.name].str_value == "y"
                ]
            except Exception:  # pylint: disable=broad-exception-caught
                return []

    def find_newly_exposed_options(
        self, sym_name: str, new_val: str
    ) -> List[str]:
        """Find Kconfig options that become visible when sym_name is changed."""
        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return []

            orig_visible = set()
            for name, sym in self.kconf.syms.items():
                if has_prompt(sym) and getattr(sym, "visibility", 0) > 0:
                    orig_visible.add(name)

            target_sym = self.kconf.syms[sym_name]
            orig_val = target_sym.str_value
            orig_choice_sel = (
                target_sym.choice.selection if target_sym.choice else None
            )
            new_visible = set()
            try:
                target_sym.set_value(new_val)
                for name, sym in self.kconf.syms.items():
                    if has_prompt(sym) and getattr(sym, "visibility", 0) > 0:
                        new_visible.add(name)
            except Exception:  # pylint: disable=broad-exception-caught
                return []
            finally:
                try:
                    if orig_choice_sel is not None:
                        orig_choice_sel.set_value("y")
                    else:
                        target_sym.set_value(orig_val)
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

            exposed = list(new_visible - orig_visible)
            return sorted(exposed)

    def find_newly_exposed_candidates(
        self,
        sym_name: str,
        new_val: str,
        include_memory_options: bool = False,
    ) -> List[Dict[str, str]]:
        """Find candidate Kconfig actions for options exposed by sym_name=new_val."""
        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return []

            orig_visible = {
                name
                for name, sym in self.kconf.syms.items()
                if has_prompt(sym) and getattr(sym, "visibility", 0) > 0
            }

            target_sym = self.kconf.syms[sym_name]
            orig_val = target_sym.str_value
            orig_choice_sel = (
                target_sym.choice.selection if target_sym.choice else None
            )
            exposed_cands = []
            try:
                target_sym.set_value(new_val)
                new_visible = {
                    name
                    for name, sym in self.kconf.syms.items()
                    if has_prompt(sym) and getattr(sym, "visibility", 0) > 0
                }
                parent_act = f"CONFIG_{sym_name}={new_val}"
                seen_acts = set()
                for exp_sym in sorted(new_visible - orig_visible):
                    if self.is_mandatory_symbol(exp_sym):
                        continue
                    typed_cands = self.get_symbol_candidates(
                        exp_sym, include_memory_options
                    )
                    for opt_name, t_val, act in typed_cands:
                        if act in seen_acts:
                            continue
                        cand_cur_val = (
                            self.get_symbol_value(opt_name, default="n") or "n"
                        )
                        if cand_cur_val == t_val:
                            continue
                        seen_acts.add(act)
                        exposed_cands.append(
                            {
                                "name": opt_name,
                                "option": opt_name,
                                "current_val": cand_cur_val,
                                "new_val": t_val,
                                "action": act,
                                "dependency_path": parent_act,
                            }
                        )
            except Exception:  # pylint: disable=broad-exception-caught
                return []
            finally:
                try:
                    if orig_choice_sel is not None:
                        orig_choice_sel.set_value("y")
                    else:
                        target_sym.set_value(orig_val)
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

            return exposed_cands

    # pylint: disable=too-many-return-statements
    def get_symbol_candidates(
        self, sym_name: str, include_memory_options: bool = False
    ) -> List[Tuple[str, str, str]]:
        """
        Get valid candidate value variations for sym_name based on type:
        - Bool / Tristate: toggle y <-> n
        - Choice: test setting alternative choices to 'y'
        - Int / Log Levels: test lower levels (0 to N-1) for _LEVEL options,
          or memory sizes if include_memory_options is True.
        Returns list of (option_name, new_val, action_string).
        """
        if sym_name in {
            "CROS_EC_RO",
            "CROS_EC_RW",
            "CROS_EC_ACTIVE_COPY",
        }:
            return []

        with self._lock:
            if not self.kconf or sym_name not in self.kconf.syms:
                return []

            sym = self.kconf.syms[sym_name]
            if not has_prompt(sym):
                return []

            cur_val = sym.str_value
            results = []

            # Handle Choice Symbols
            if sym.choice and sym.choice.syms:
                for alt in sym.choice.syms:
                    if (
                        alt.name != sym_name
                        and getattr(alt, "visibility", 0) > 0
                    ):
                        results.append((alt.name, "y", f"CONFIG_{alt.name}=y"))
                return results

            # Handle Bool / Tristate Symbols
            if sym.type in (kconfiglib.BOOL, kconfiglib.TRISTATE):
                target_val = "n" if cur_val in ["y", "1"] else "y"
                target_tri = 0 if target_val == "n" else 2
                if (
                    hasattr(sym, "assignable")
                    and target_tri not in sym.assignable
                ):
                    return []
                results.append(
                    (sym_name, target_val, f"CONFIG_{sym_name}={target_val}")
                )
                return results

            # Handle Int / Level Symbols
            if sym.type == kconfiglib.INT:
                if (
                    sym_name in MEMORY_TUNING_KCONFIGS
                    and not include_memory_options
                ):
                    return []
                if (
                    sym_name in MEMORY_TUNING_KCONFIGS
                    and include_memory_options
                ):
                    test_vals = MEMORY_TUNING_KCONFIGS[sym_name]
                    for tv in test_vals:
                        if tv != cur_val:
                            results.append(
                                (sym_name, tv, f"CONFIG_{sym_name}={tv}")
                            )
                    return results

                # Log / Assert / Debug level integers: test values from
                # (cur_val - 1) down to 0
                if any(
                    sym_name.endswith(suffix)
                    for suffix in ["_LEVEL", "_LOG_LEVEL", "_DEBUG_LEVEL"]
                ):
                    try:
                        cur_lvl = int(cur_val)
                        for lvl in range(cur_lvl - 1, -1, -1):
                            results.append(
                                (sym_name, str(lvl), f"CONFIG_{sym_name}={lvl}")
                            )
                    except ValueError:
                        # Inactive or non-numeric symbol; skip candidate generation
                        return []
                    return results

            return []


# ==============================================================================
# Build & Test Runner Class
# ==============================================================================


class ZephyrBuildRunner:
    """Manages Zmake configuration and Ninja builds for size measurements."""

    def __init__(
        self,
        target: str,
        ec_base: pathlib.Path,
        build_root: pathlib.Path,
        image_type: str = "rw",
    ):
        self.target = target
        self.ec_base = ec_base
        self.build_root = build_root
        self.image_type = image_type.lower()
        self.project_build_dir = self.build_root / self.target
        self.build_ro_dir = self.project_build_dir / "build-ro"
        self.build_rw_dir = self.project_build_dir / "build-rw"
        self.kconfig_ro_file = self.project_build_dir / "kconfig-ro.conf"
        self.kconfig_rw_file = self.project_build_dir / "kconfig-rw.conf"
        self.zmake_path = self.ec_base / "zephyr" / "zmake"

    def configure_project(self) -> bool:
        """Run zmake configure to set up project build directory."""
        logging.info(
            "Configuring project target '%s' (Image Type: %s) in %s...",
            self.target,
            self.image_type,
            self.project_build_dir,
        )
        if self.project_build_dir.exists():
            shutil.rmtree(self.project_build_dir, ignore_errors=True)
        env = os.environ.copy()
        env["PYTHONPATH"] = str(self.zmake_path)
        cmd = [
            sys.executable,
            "-m",
            "zmake",
            "configure",
            "--clobber",
            "-B",
            str(self.build_root),
            self.target,
        ]
        proc = subprocess.run(
            cmd,
            cwd=self.ec_base,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        if proc.returncode != 0:
            logging.error(
                "Failed to configure target '%s':\n%s", self.target, proc.stdout
            )
            return False
        return True

    def build_and_measure(
        self,
    ) -> Tuple[Optional[int], Optional[int], bool, float, str]:
        """Run ninja build and measure Flash and RAM usage for image_type."""
        start_t = time.time()
        if self.image_type == "both":
            target_dirs = [self.build_ro_dir, self.build_rw_dir]
        elif self.image_type == "ro":
            target_dirs = [self.build_ro_dir]
        else:
            target_dirs = [self.build_rw_dir]

        total_flash = 0
        total_ram = 0
        outputs = []
        for target_dir in target_dirs:
            # Force zephyr.elf relink so Ninja always prints Memory region sizes
            (target_dir / "zephyr" / "zephyr.elf").unlink(missing_ok=True)
            cmd = ["ninja", "-C", str(target_dir)]
            proc = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                check=False,
            )
            outputs.append(proc.stdout)
            if proc.returncode != 0:
                elapsed = time.time() - start_t
                return None, None, False, elapsed, "\n".join(outputs)
            map_file = target_dir / "zephyr" / "zephyr.map"
            flash, ram = parse_memory_usage(proc.stdout, map_path=map_file)
            if flash == 0 or ram == 0:
                elapsed = time.time() - start_t
                return None, None, False, elapsed, "\n".join(outputs)
            total_flash += flash
            total_ram += ram

        elapsed = time.time() - start_t
        return total_flash, total_ram, True, elapsed, "\n".join(outputs)

    def test_kconfig_changes(
        self, changes: List[str]
    ) -> Tuple[Optional[int], Optional[int], bool, float, str]:
        """Test Kconfig lines by appending to kconfig-*.conf and building."""
        conf_files = []
        if self.image_type in ["rw", "both"]:
            if self.kconfig_rw_file.exists():
                conf_files.append(self.kconfig_rw_file)
        if self.image_type in ["ro", "both"]:
            if self.kconfig_ro_file.exists():
                conf_files.append(self.kconfig_ro_file)

        if not conf_files:
            logging.error(
                "No valid Kconfig conf file found for image_type %s",
                self.image_type,
            )
            return None, None, False, 0.0, ""

        orig_contents = {}
        for cf in conf_files:
            with open(cf, "r", encoding="utf-8") as f:
                orig_contents[cf] = f.read()

        try:
            for cf in conf_files:
                with open(cf, "a", encoding="utf-8") as f:
                    f.write("\n# Size Optimizer Test Modifications\n")
                    for line in changes:
                        f.write(f"{line}\n")

            flash, ram, success, elapsed, log_output = self.build_and_measure()
            return flash, ram, success, elapsed, log_output
        finally:
            for cf, orig in orig_contents.items():
                with open(cf, "w", encoding="utf-8") as f:
                    f.write(orig)


# ==============================================================================
# Kconfig Optimizer & Report Generator
# ==============================================================================


class ZephyrKconfigOptimizer:
    """Orchestrates Kconfig size impact search and generates reports."""

    # pylint: disable=too-many-arguments
    def __init__(
        self,
        target: str,
        mode: str = "full",
        target_metric: str = "both",
        max_tests: int = 0,
        include_memory_options: bool = False,
        jobs: int = 1,
        image_type: str = "rw",
        log_options: str = "include",
        build_dir: Optional[pathlib.Path] = None,
        ec_dir: Optional[pathlib.Path] = None,
    ):
        self.target = target
        self.mode = mode.lower()
        self.target_metric = target_metric.lower()
        self.max_tests = max_tests
        self.include_memory_options = include_memory_options
        self.jobs = max(1, jobs)
        self.image_type = image_type.lower()
        self.log_options = log_options.lower()
        self.ec_base = ec_dir or EC_BASE
        self.build_root = build_dir or (
            self.ec_base / "build" / f"zmake_opt_{target}"
        )
        self.runner = ZephyrBuildRunner(
            target, self.ec_base, self.build_root, image_type=self.image_type
        )
        self.resolver = None
        self.results = []
        self.baseline_flash = 0
        self.baseline_ram = 0

    def run(self) -> Dict[str, Any]:
        """Execute the optimization process."""
        logging.info(
            "Starting Zephyr EC Kconfig Size Optimizer for target '%s'",
            self.target,
        )
        logging.info(
            "Mode: %s | Image Type: %s | Parallel Jobs: %d | "
            "Include Memory: %s",
            self.mode,
            self.image_type,
            self.jobs,
            self.include_memory_options,
        )

        # Step 1: Configure project
        if not self.runner.configure_project():
            raise RuntimeError(
                f"Configuration failed for target '{self.target}'"
            )

        # Step 2: Measure Baseline
        logging.info(
            "Measuring baseline build size (%s)...", self.image_type.upper()
        )
        flash, ram, success, elapsed, log = self.runner.build_and_measure()
        if not success:
            raise RuntimeError(f"Baseline build failed:\n{log}")

        self.baseline_flash = flash or 0
        self.baseline_ram = ram or 0
        logging.info(
            "Baseline Size (%s): Flash = %d bytes (%.2f KB), "
            "RAM = %d bytes (%.2f KB) [Took %.2fs]",
            self.image_type.upper(),
            self.baseline_flash,
            self.baseline_flash / 1024.0,
            self.baseline_ram,
            self.baseline_ram / 1024.0,
            elapsed,
        )

        # Step 3: Initialize Dependency Resolver
        target_kconf_dir = (
            self.runner.build_ro_dir
            if self.image_type == "ro"
            else self.runner.build_rw_dir
        )
        self.resolver = KconfigDependencyResolver(
            ZEPHYR_BASE, self.ec_base, target_kconf_dir
        )

        # Step 4: Collect Candidate Kconfigs to Test
        candidates = self._collect_candidates()
        logging.info(
            "Identified %d candidate Kconfigs for evaluation", len(candidates)
        )

        if self.max_tests > 0:
            candidates = candidates[: self.max_tests]
            logging.info(
                "Limiting search to first %d candidates (--max-tests)",
                len(candidates),
            )

        # Step 5: Evaluate Candidates
        self._evaluate_candidates(candidates)

        return {
            "target": self.target,
            "baseline": {
                "flash": self.baseline_flash,
                "ram": self.baseline_ram,
            },
            "results": self.results,
        }

    def _collect_candidates(self) -> List[Dict[str, Any]]:
        """Collect candidate Kconfigs based on search mode (full/recursive)."""
        candidates = []
        seen_actions = set()
        seen_options = set()
        active_configs = self._get_active_configs()

        # Optional Memory and Stack Tuning Kconfigs
        # (if --include-memory-options is enabled)
        if self.include_memory_options:
            for sym_name, test_vals in MEMORY_TUNING_KCONFIGS.items():
                config_key = f"CONFIG_{sym_name}"
                actual_val = active_configs.get(config_key, "n")
                if self.resolver:
                    actual_val = self.resolver.get_symbol_value(
                        sym_name, default=actual_val
                    )

                # Ignore redundant inactive-to-zero transitions for int configs
                if actual_val in ["n", ""] and test_vals == ["0"]:
                    continue

                for t_val in test_vals:
                    if t_val != actual_val:
                        act = f"CONFIG_{sym_name}={t_val}"
                        if act not in seen_actions:
                            seen_actions.add(act)
                            seen_options.add(sym_name)
                            candidates.append(
                                {
                                    "name": sym_name,
                                    "option": sym_name,
                                    "current_val": actual_val,
                                    "new_val": t_val,
                                    "action": act,
                                }
                            )

        # Full / Recursive Mode: Discover active non-mandatory Kconfigs
        for config_key, val in active_configs.items():
            sym_name = config_key.replace("CONFIG_", "")
            is_mand = (
                self.resolver.is_mandatory_symbol(sym_name)
                if self.resolver
                else is_mandatory_kconfig(sym_name)
            )
            if sym_name in MEMORY_TUNING_KCONFIGS or is_mand:
                continue

            # Check if prompt-bearing / configurable in kconfiglib
            if self.resolver and self.resolver.has_symbol(sym_name):
                typed_cands = self.resolver.get_symbol_candidates(
                    sym_name, self.include_memory_options
                )
                for opt_name, t_val, act in typed_cands:
                    cand_cur_val = self.resolver.get_symbol_value(
                        opt_name, default=val
                    )
                    if cand_cur_val == t_val:
                        continue
                    if act not in seen_actions:
                        seen_actions.add(act)
                        seen_options.add(opt_name)
                        candidates.append(
                            {
                                "name": opt_name,
                                "option": opt_name,
                                "current_val": cand_cur_val,
                                "new_val": t_val,
                                "action": act,
                            }
                        )
            else:
                if val in ["y", "1"]:
                    act = f"CONFIG_{sym_name}=n"
                    if act not in seen_actions:
                        seen_actions.add(act)
                        seen_options.add(sym_name)
                        candidates.append(
                            {
                                "name": sym_name,
                                "option": sym_name,
                                "current_val": val,
                                "new_val": "n",
                                "action": act,
                            }
                        )

        # Apply Log Options Filtering (include, exclude, only)
        if self.log_options == "exclude":
            candidates = [
                c for c in candidates if not is_log_kconfig(c["name"])
            ]
        elif self.log_options == "only":
            candidates = [c for c in candidates if is_log_kconfig(c["name"])]

        # Recursive Mode: Find newly exposed options enabled by toggling parents
        if self.mode == "recursive" and self.resolver:
            logging.info(
                "Running recursive dependency exploration to discover "
                "hidden Kconfigs..."
            )
            exposed_candidates = []
            hidden_options = set()
            explore_set = (
                candidates[: self.max_tests]
                if self.max_tests > 0
                else candidates[:]
            )
            for cand in explore_set:
                s_name = cand["name"]
                test_v = cand["new_val"]
                if test_v not in ("y", "n"):
                    continue
                for exp_cand in self.resolver.find_newly_exposed_candidates(
                    s_name, test_v, self.include_memory_options
                ):
                    opt_name = exp_cand["name"]
                    act = exp_cand["action"]
                    if self.log_options == "exclude" and is_log_kconfig(
                        opt_name
                    ):
                        continue
                    if self.log_options == "only" and not is_log_kconfig(
                        opt_name
                    ):
                        continue
                    hidden_options.add(opt_name)
                    if act not in seen_actions:
                        seen_actions.add(act)
                        seen_options.add(opt_name)
                        exposed_candidates.append(exp_cand)

            logging.info(
                "Discovered %d newly exposed hidden Kconfig options "
                "(%d candidate actions)!",
                len(hidden_options),
                len(exposed_candidates),
            )
            candidates.extend(exposed_candidates)

        if self.log_options == "exclude":
            logging.info(
                "Applied --log-options=exclude: %d non-log candidates remain",
                len(candidates),
            )
        elif self.log_options == "only":
            logging.info(
                "Applied --log-options=only: %d log-related candidates remain",
                len(candidates),
            )

        return candidates

    def _get_active_configs(self) -> Dict[str, str]:
        """Read active configs from target build dir .config."""
        if self.image_type == "both":
            target_dirs = [self.runner.build_ro_dir, self.runner.build_rw_dir]
        elif self.image_type == "ro":
            target_dirs = [self.runner.build_ro_dir]
        else:
            target_dirs = [self.runner.build_rw_dir]

        active = {}
        for target_dir in target_dirs:
            config_path = target_dir / "zephyr" / ".config"
            if not config_path.exists():
                continue

            with open(config_path, "r", encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if line and not line.startswith("#") and "=" in line:
                        k, v = line.split("=", 1)
                        active[k] = v
        return active

    def _find_missing_dep_symbols(
        self, error_log: str
    ) -> List[Tuple[str, str, str]]:
        """Parse build error log to find missing deps and suggest repairs."""
        repair_actions = []
        seen_syms = set()

        # 1. Look for Kconfig 'is currently being y-selected by...' in log
        for match in re.finditer(
            r"is currently being y-selected by the following symbols:"
            r"\s*\n\s*-\s*([A-Z0-9_]+)",
            error_log,
        ):
            sel_sym = match.group(1)
            if sel_sym not in seen_syms and not is_mandatory_kconfig(sel_sym):
                seen_syms.add(sel_sym)
                repair_actions.append((sel_sym, "n", f"CONFIG_{sel_sym}=n"))

        # 2. Look for Kconfig warning dependencies in log:
        # e.g. 'FOO has direct dependencies BAR with value n'
        for match in re.finditer(
            r"warning:\s+([A-Z0-9_]+).*?dependencies\s+([A-Z0-9_]+)",
            error_log,
            re.DOTALL,
        ):
            sym1, sym2 = match.group(1), match.group(2)
            if sym1 not in seen_syms and not is_mandatory_kconfig(sym1):
                seen_syms.add(sym1)
                repair_actions.append((sym1, "n", f"CONFIG_{sym1}=n"))
            if sym2 not in seen_syms and not is_mandatory_kconfig(sym2):
                seen_syms.add(sym2)
                repair_actions.append((sym2, "n", f"CONFIG_{sym2}=n"))

        # 3. Look for undefined references to cros_ec or zephyr drivers:
        # e.g. 'undefined reference to `cros_ec_accel_...'
        for match in re.finditer(
            r"undefined reference to [`']cros_ec_([a-z0-9_]+)", error_log
        ):
            sub_name = match.group(1).upper()
            sym_candidate = f"PLATFORM_EC_{sub_name}"
            if (
                sym_candidate not in seen_syms
                and self.resolver
                and self.resolver.has_symbol(sym_candidate)
            ):
                seen_syms.add(sym_candidate)
                repair_actions.append(
                    (sym_candidate, "n", f"CONFIG_{sym_candidate}=n")
                )

        # 4. Look for undeclared CONFIG_ symbol errors in GCC output:
        # e.g. 'error: 'CONFIG_FOO' undeclared'
        for match in re.finditer(
            r"['`]CONFIG_([A-Z0-9_]+)['`]\s+undeclared", error_log
        ):
            sym_candidate = match.group(1)
            is_bool_sym = (
                self.resolver.is_bool_or_tristate(sym_candidate)
                if self.resolver
                else True
            )
            if (
                is_bool_sym
                and sym_candidate not in seen_syms
                and not is_mandatory_kconfig(sym_candidate)
            ):
                seen_syms.add(sym_candidate)
                repair_actions.append(
                    (sym_candidate, "n", f"CONFIG_{sym_candidate}=n")
                )

        # 5. Look for mutually exclusive option errors in compiler/Kconfig:
        # e.g. '#error "CONFIG_FOO and CONFIG_BAR are mutually exclusive."'
        for match in re.finditer(
            r"['\"]?CONFIG_([A-Z0-9_]+)['\"]?\s+and\s+"
            r"['\"]?CONFIG_([A-Z0-9_]+)['\"]?\s+are\s+mutually\s+exclusive",
            error_log,
            re.IGNORECASE,
        ):
            sym1, sym2 = match.group(1), match.group(2)
            if sym1 not in seen_syms and not is_mandatory_kconfig(sym1):
                seen_syms.add(sym1)
                repair_actions.append((sym1, "n", f"CONFIG_{sym1}=n"))
            if sym2 not in seen_syms and not is_mandatory_kconfig(sym2):
                seen_syms.add(sym2)
                repair_actions.append((sym2, "n", f"CONFIG_{sym2}=n"))

        return repair_actions

    def _evaluate_candidates(self, candidates: List[Dict[str, Any]]):
        """Test each candidate Kconfig option and record size impact."""
        total = len(candidates)

        if self.jobs <= 1:
            # Sequential evaluation using primary runner
            for idx, item in enumerate(candidates, 1):
                sym_name = item["name"]
                opt_name = item["option"]
                cur_val = item["current_val"]
                new_val = item["new_val"]
                action = item["action"]
                logging.info(
                    "[%d/%d] Testing %s (%s -> %s)...",
                    idx,
                    total,
                    opt_name,
                    cur_val,
                    new_val,
                )

                # Get side-effect dependencies before build
                side_effects = []
                base_actions = [action]
                if item.get("dependency_path"):
                    side_effects.append(f"[Path: {item['dependency_path']}]")
                    base_actions = [item["dependency_path"], action]

                if self.resolver:
                    raw_effects = self.resolver.get_side_effects(
                        sym_name, new_val
                    )
                    side_effects.extend(
                        [
                            f"CONFIG_{e[0]} ({e[1]} -> {e[2]})"
                            for e in raw_effects
                        ]
                    )

                # Run build test
                flash, ram, success, elapsed, log_out = (
                    self.runner.test_kconfig_changes(base_actions)
                )

                if success and flash is not None and ram is not None:
                    d_flash = self.baseline_flash - flash
                    d_ram = self.baseline_ram - ram
                    pct_flash = (
                        (d_flash / self.baseline_flash * 100.0)
                        if self.baseline_flash
                        else 0.0
                    )
                    pct_ram = (
                        (d_ram / self.baseline_ram * 100.0)
                        if self.baseline_ram
                        else 0.0
                    )

                    logging.info(
                        " -> Flash Savings: %+d B (%.2f%%) | "
                        "RAM Savings: %+d B (%.2f%%) | Time: %.2fs",
                        d_flash,
                        pct_flash,
                        d_ram,
                        pct_ram,
                        elapsed,
                    )

                    self.results.append(
                        {
                            "name": sym_name,
                            "option": opt_name,
                            "current_val": cur_val,
                            "new_val": new_val,
                            "action": action,
                            "success": True,
                            "flash_savings": d_flash,
                            "ram_savings": d_ram,
                            "flash_pct": pct_flash,
                            "ram_pct": pct_ram,
                            "side_effects": side_effects,
                            "dependent_changes": [],
                            "test_time": elapsed,
                        }
                    )
                else:
                    # Attempt Auto-Repair (Capped at 1 compound attempt)
                    repair_acts = []
                    if self.resolver:
                        selectors = self.resolver.is_selected_by_others(
                            sym_name
                        )
                        for sel in selectors:
                            if not is_mandatory_kconfig(sel):
                                repair_acts.append(
                                    (sel, "n", f"CONFIG_{sel}=n")
                                )
                    repair_acts.extend(self._find_missing_dep_symbols(log_out))
                    repaired_success = False
                    if repair_acts:
                        repair_sym, repair_val, repair_act = repair_acts[0]
                        orig_repair_val = (
                            self.resolver.get_symbol_value(
                                repair_sym, default="y"
                            )
                            if self.resolver
                            else "y"
                        )
                        logging.info(
                            " -> Attempting Auto-Repair for %s: "
                            "Appending %s...",
                            opt_name,
                            repair_act,
                        )
                        r_flash, r_ram, r_succ, r_elapsed, _ = (
                            self.runner.test_kconfig_changes(
                                base_actions + [repair_act]
                            )
                        )
                        if r_succ and r_flash is not None and r_ram is not None:
                            d_flash = self.baseline_flash - r_flash
                            d_ram = self.baseline_ram - r_ram
                            pct_flash = (
                                (d_flash / self.baseline_flash * 100.0)
                                if self.baseline_flash
                                else 0.0
                            )
                            pct_ram = (
                                (d_ram / self.baseline_ram * 100.0)
                                if self.baseline_ram
                                else 0.0
                            )
                            logging.info(
                                " -> Auto-Repair SUCCESS! %s + %s: "
                                "Flash %+dB | RAM %+dB [Took %.2fs]",
                                action,
                                repair_act,
                                d_flash,
                                d_ram,
                                r_elapsed,
                            )
                            repaired_val = (
                                f"{new_val} (Auto-Repaired with "
                                f"{repair_sym}={repair_val})"
                            )
                            self.results.append(
                                {
                                    "name": sym_name,
                                    "option": opt_name,
                                    "current_val": cur_val,
                                    "new_val": repaired_val,
                                    "action": f"{action} && {repair_act}",
                                    "success": True,
                                    "flash_savings": d_flash,
                                    "ram_savings": d_ram,
                                    "flash_pct": pct_flash,
                                    "ram_pct": pct_ram,
                                    "side_effects": side_effects
                                    + [f"[Auto-Repair: {repair_act}]"],
                                    "dependent_changes": [
                                        f"CONFIG_{repair_sym} "
                                        f"({orig_repair_val} -> {repair_val})"
                                    ],
                                    "test_time": elapsed + r_elapsed,
                                    "auto_repaired": True,
                                }
                            )
                            repaired_success = True

                    if not repaired_success:
                        reason_str = extract_failure_reason(log_out)
                        logging.info(
                            " -> Build FAILED with %s (%s) (Time: %.2fs)",
                            action,
                            reason_str,
                            elapsed,
                        )
                        self.results.append(
                            {
                                "name": sym_name,
                                "option": opt_name,
                                "current_val": cur_val,
                                "new_val": new_val,
                                "action": action,
                                "success": False,
                                "flash_savings": 0,
                                "ram_savings": 0,
                                "flash_pct": 0.0,
                                "ram_pct": 0.0,
                                "side_effects": side_effects,
                                "dependent_changes": [],
                                "failure_reason": reason_str,
                                "test_time": elapsed,
                            }
                        )
        else:
            # Parallel evaluation with N worker build directories
            logging.info(
                "Initializing %d parallel worker build directories...",
                self.jobs,
            )
            worker_runners = []
            try:
                for i in range(self.jobs):
                    w_root = pathlib.Path(f"{self.build_root}_w{i}")
                    w_runner = ZephyrBuildRunner(
                        self.target,
                        self.ec_base,
                        w_root,
                        image_type=self.image_type,
                    )
                    if not w_runner.configure_project():
                        raise RuntimeError(
                            f"Failed to configure worker {i} in {w_root}"
                        )
                    worker_runners.append(w_runner)

                runner_queue = queue.Queue()
                for r in worker_runners:
                    runner_queue.put(r)

                def _eval_task(task_arg):
                    idx, item = task_arg
                    sym_name = item["name"]
                    new_val = item["new_val"]
                    action = item["action"]

                    w_runner = runner_queue.get()
                    try:
                        # Side effects
                        side_effects = []
                        base_actions = [action]
                        if item.get("dependency_path"):
                            side_effects.append(
                                f"[Path: {item['dependency_path']}]"
                            )
                            base_actions = [item["dependency_path"], action]

                        if self.resolver:
                            raw_effects = self.resolver.get_side_effects(
                                sym_name, new_val
                            )
                            side_effects.extend(
                                [
                                    f"CONFIG_{e[0]} ({e[1]} -> {e[2]})"
                                    for e in raw_effects
                                ]
                            )

                        flash, ram, success, elapsed, log_out = (
                            w_runner.test_kconfig_changes(base_actions)
                        )

                        # Attempt Auto-Repair if build failed
                        dependent_changes = []
                        if not success:
                            candidate_repairs = []
                            if self.resolver:
                                selectors = self.resolver.is_selected_by_others(
                                    sym_name
                                )
                                for sel in selectors:
                                    if not is_mandatory_kconfig(sel):
                                        candidate_repairs.append(
                                            (sel, "n", f"CONFIG_{sel}=n")
                                        )

                            log_repairs = self._find_missing_dep_symbols(
                                log_out
                            )
                            candidate_repairs.extend(log_repairs)

                            for (
                                repair_sym,
                                repair_val,
                                repair_act,
                            ) in candidate_repairs[:1]:
                                orig_repair_val = (
                                    self.resolver.get_symbol_value(
                                        repair_sym, default="y"
                                    )
                                    if self.resolver
                                    else "y"
                                )
                                r_flash, r_ram, r_succ, r_elapsed, _ = (
                                    w_runner.test_kconfig_changes(
                                        base_actions + [repair_act]
                                    )
                                )
                                if (
                                    r_succ
                                    and r_flash is not None
                                    and r_ram is not None
                                ):
                                    dependent_changes.append(
                                        f"CONFIG_{repair_sym} "
                                        f"({orig_repair_val} -> {repair_val})"
                                    )
                                    repaired_val = (
                                        f"{new_val} (Auto-Repaired with "
                                        f"{repair_sym}={repair_val})"
                                    )
                                    return (
                                        idx,
                                        item,
                                        r_flash,
                                        r_ram,
                                        True,
                                        elapsed + r_elapsed,
                                        side_effects,
                                        dependent_changes,
                                        repaired_val,
                                        f"{action} && {repair_act}",
                                        True,
                                        log_out,
                                    )

                        return (
                            idx,
                            item,
                            flash,
                            ram,
                            success,
                            elapsed,
                            side_effects,
                            dependent_changes,
                            new_val,
                            action,
                            False,
                            log_out,
                        )
                    finally:
                        runner_queue.put(w_runner)

                with concurrent.futures.ThreadPoolExecutor(
                    max_workers=self.jobs
                ) as executor:
                    futures = [
                        executor.submit(_eval_task, (idx, item))
                        for idx, item in enumerate(candidates, 1)
                    ]
                    for future in concurrent.futures.as_completed(futures):
                        res_tuple = future.result()
                        (
                            idx,
                            item,
                            flash,
                            ram,
                            success,
                            elapsed,
                            side_effects,
                            dependent_changes,
                            effective_new_val,
                            effective_action,
                            is_repaired,
                            log_out,
                        ) = res_tuple
                        sym_name = item["name"]
                        opt_name = item["option"]
                        cur_val = item["current_val"]

                        if success and flash is not None and ram is not None:
                            d_flash = self.baseline_flash - flash
                            d_ram = self.baseline_ram - ram
                            pct_flash = (
                                (d_flash / self.baseline_flash * 100.0)
                                if self.baseline_flash
                                else 0.0
                            )
                            pct_ram = (
                                (d_ram / self.baseline_ram * 100.0)
                                if self.baseline_ram
                                else 0.0
                            )

                            repair_tag = (
                                " [AUTO-REPAIRED]" if is_repaired else ""
                            )
                            logging.info(
                                "[%d/%d] %s (%s -> %s)%s: "
                                "Flash %+-6dB (%.1f%%) | "
                                "RAM %+-6dB (%.1f%%) [Took %.1fs]",
                                idx,
                                total,
                                opt_name,
                                cur_val,
                                effective_new_val,
                                repair_tag,
                                d_flash,
                                pct_flash,
                                d_ram,
                                pct_ram,
                                elapsed,
                            )

                            self.results.append(
                                {
                                    "name": sym_name,
                                    "option": opt_name,
                                    "current_val": cur_val,
                                    "new_val": effective_new_val,
                                    "action": effective_action,
                                    "success": True,
                                    "flash_savings": d_flash,
                                    "ram_savings": d_ram,
                                    "flash_pct": pct_flash,
                                    "ram_pct": pct_ram,
                                    "side_effects": side_effects,
                                    "dependent_changes": dependent_changes,
                                    "test_time": elapsed,
                                    "auto_repaired": is_repaired,
                                }
                            )
                        else:
                            reason_str = extract_failure_reason(log_out)
                            logging.info(
                                "[%d/%d] %s (%s -> %s): Build FAILED (%s) "
                                "[Took %.1fs]",
                                idx,
                                total,
                                opt_name,
                                cur_val,
                                effective_new_val,
                                reason_str,
                                elapsed,
                            )
                            self.results.append(
                                {
                                    "name": sym_name,
                                    "option": opt_name,
                                    "current_val": cur_val,
                                    "new_val": effective_new_val,
                                    "action": effective_action,
                                    "success": False,
                                    "flash_savings": 0,
                                    "ram_savings": 0,
                                    "flash_pct": 0.0,
                                    "ram_pct": 0.0,
                                    "side_effects": side_effects,
                                    "dependent_changes": [],
                                    "failure_reason": reason_str,
                                    "test_time": elapsed,
                                }
                            )
            finally:
                for w_runner in worker_runners:
                    shutil.rmtree(w_runner.build_root, ignore_errors=True)

        # Sort results by size savings based on target_metric
        def sort_key(res):
            """Sort key for ranking candidates by size savings."""
            if not res["success"]:
                return -99999999
            if self.target_metric == "flash":
                return res["flash_savings"]
            if self.target_metric == "ram":
                return res["ram_savings"]
            return res["flash_savings"] + res["ram_savings"]

        self.results.sort(key=sort_key, reverse=True)


# ==============================================================================
# Report Generators
# ==============================================================================


def generate_markdown_report(report_data: Dict[str, Any]) -> str:
    """Generate Markdown report summarizing size impact and side effects."""
    target = report_data["target"]
    baseline = report_data["baseline"]
    results = report_data["results"]

    flash_kb = baseline["flash"] / 1024.0
    ram_kb = baseline["ram"] / 1024.0

    lines = []
    lines.append(f"# Zephyr EC Kconfig Size Optimization Report: `{target}`")
    lines.append("")
    lines.append("## Executive Summary")
    lines.append("")
    lines.append(f"- **Target Build**: `{target}`")
    lines.append(
        f"- **Baseline Flash Size**: {baseline['flash']:,} Bytes "
        f"({flash_kb:.2f} KB)"
    )
    lines.append(
        f"- **Baseline RAM Size**: {baseline['ram']:,} Bytes "
        f"({ram_kb:.2f} KB)"
    )
    lines.append(f"- **Total Kconfigs Evaluated**: {len(results)}")
    lines.append("")

    # Recommendations Table
    lines.append("## Kconfig Size Impact Table")
    lines.append("")
    lines.append(
        "| Option | Current Value | New Value | Flash Savings | RAM Savings | "
        "Side Effects |"
    )
    lines.append("| :--- | :--- | :--- | :--- | :--- | :--- |")

    for res in results:
        if not res["success"]:
            continue
        opt_str = f"`{res.get('option', res['name'])}`"
        cur_val = f"`{res.get('current_val', 'y')}`"
        new_val = f"`{res.get('new_val', 'n')}`"
        if res["flash_savings"] > 0:
            flash_str = (
                f"**-{res['flash_savings']:,} B** ({res['flash_pct']:.1f}%)"
            )
        elif res["flash_savings"] < 0:
            flash_str = f"+{-res['flash_savings']:,} B"
        else:
            flash_str = "0 B"

        if res["ram_savings"] > 0:
            ram_str = f"**-{res['ram_savings']:,} B** ({res['ram_pct']:.1f}%)"
        elif res["ram_savings"] < 0:
            ram_str = f"+{-res['ram_savings']:,} B"
        else:
            ram_str = "0 B"
        side_effects = (
            ", ".join(res["side_effects"]) if res["side_effects"] else ""
        )
        lines.append(
            f"| {opt_str} | {cur_val} | {new_val} | "
            f"{flash_str} | {ram_str} | {side_effects} |"
        )

    lines.append("")

    # Failed / Incompatible Options section if any
    failed_opts = [r for r in results if not r["success"]]
    if failed_opts:
        lines.append("## Incompatible / Failed Kconfig Changes")
        lines.append("")
        lines.append(
            "The following Kconfigs failed to compile when modified "
            "individually:"
        )
        lines.append("")
        for f_opt in failed_opts:
            opt_str = f_opt.get("option", f_opt["name"])
            cur_v = f_opt.get("current_val", "y")
            new_v = f_opt.get("new_val", "n")
            reason = f_opt.get("failure_reason", "Build compilation failed.")
            lines.append(f"- `{opt_str}` (`{cur_v}` -> `{new_v}`): {reason}")
        lines.append("")

    return "\n".join(lines)


def generate_text_report(report_data: Dict[str, Any]) -> str:
    """Generate plain text report for terminal output."""
    target = report_data["target"]
    baseline = report_data["baseline"]
    results = report_data["results"]

    flash_kb = baseline["flash"] / 1024.0
    ram_kb = baseline["ram"] / 1024.0
    sep_eq = "=" * 100
    sep_dash = "-" * 100

    lines = []
    lines.append(sep_eq)
    lines.append(f" Zephyr EC Kconfig Size Optimization Report: {target}")
    lines.append(sep_eq)
    lines.append(f"Baseline Flash: {baseline['flash']:,} B ({flash_kb:.2f} KB)")
    lines.append(f"Baseline RAM:   {baseline['ram']:,} B ({ram_kb:.2f} KB)")
    lines.append(sep_dash)
    lines.append(
        f"{'Option':<32} | {'Current':<8} | {'New':<8} | "
        f"{'Flash Savings':<14} | {'RAM Savings':<14} | {'Side Effects'}"
    )
    lines.append(sep_dash)

    for res in results:
        if not res["success"]:
            continue
        opt_str = res.get("option", res["name"])
        cur_val = str(res.get("current_val", "y"))
        new_val = str(res.get("new_val", "n"))
        if res["flash_savings"] > 0:
            flash_str = f"-{res['flash_savings']:,} B"
        elif res["flash_savings"] < 0:
            flash_str = f"+{-res['flash_savings']:,} B"
        else:
            flash_str = "0 B"

        if res["ram_savings"] > 0:
            ram_str = f"-{res['ram_savings']:,} B"
        elif res["ram_savings"] < 0:
            ram_str = f"+{-res['ram_savings']:,} B"
        else:
            ram_str = "0 B"
        side_effects = (
            ", ".join(res["side_effects"]) if res["side_effects"] else ""
        )
        lines.append(
            f"{opt_str:<32} | {cur_val:<8} | {new_val:<8} | "
            f"{flash_str:<14} | {ram_str:<14} | {side_effects}"
        )

    lines.append(sep_eq)
    return "\n".join(lines)


# ==============================================================================
# Main Entry Point
# ==============================================================================


def main():
    """Main CLI entry point for Zephyr EC Kconfig Size Optimizer."""
    parser = argparse.ArgumentParser(
        description="Zephyr EC Kconfig Size Optimizer & Impact Analyzer",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "target",
        help="Build target name (e.g. 'craask', 'nivviks', 'brox', 'rex').",
    )
    parser.add_argument(
        "-m",
        "--mode",
        choices=["full", "recursive"],
        default="full",
        help=(
            "Search mode:\n"
            "  full (default): Evaluate all active non-mandatory Kconfig "
            "options in .config\n"
            "  recursive: Full mode + discover & test hidden options exposed "
            "by toggling parent features"
        ),
    )
    parser.add_argument(
        "-n",
        "--max-tests",
        type=int,
        default=0,
        help="Maximum number of Kconfig options to evaluate (0 = unlimited).",
    )
    parser.add_argument(
        "-s",
        "--include-memory-options",
        action="store_true",
        help=(
            "Include thread stack and memory pool size tuning "
            "(e.g. MAIN_STACK_SIZE, ISR_STACK_SIZE, HEAP_MEM_POOL_SIZE). "
            "Default: False (feature options only)."
        ),
    )
    parser.add_argument(
        "-b",
        "--image-type",
        choices=["rw", "ro", "both"],
        default="rw",
        help=(
            "Target firmware image to measure size impact on: "
            "'rw' (default, Read-Write image), 'ro' (Read-Only image), "
            "or 'both'."
        ),
    )
    default_jobs = max(1, (os.cpu_count() or 1) // 4)
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        nargs="?",
        const=default_jobs,
        default=default_jobs,
        help=(
            f"Number of parallel worker build directories to use for build "
            f"evaluation (default: {default_jobs} CPU cores)."
        ),
    )
    parser.add_argument(
        "-L",
        "--log-options",
        choices=["include", "exclude", "only"],
        default="include",
        help=(
            "Log/debug level Kconfig options handling: "
            "'include' (default, test log & feature options), "
            "'exclude' (test features only, exclude logs), or "
            "'only' (test log & debug options only)."
        ),
    )
    parser.add_argument(
        "-B",
        "--build-dir",
        type=pathlib.Path,
        help="Custom build directory (default: ec/build/zmake_opt_<target>).",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        help="Path to save output report file.",
    )
    parser.add_argument(
        "-f",
        "--format",
        choices=["markdown", "text", "json"],
        default="markdown",
        help="Output report format (markdown, text, json). Default: markdown.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose DEBUG logging.",
    )

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    try:
        output_path = args.output.expanduser() if args.output else None
        build_dir = args.build_dir.expanduser() if args.build_dir else None

        optimizer = ZephyrKconfigOptimizer(
            target=args.target,
            mode=args.mode,
            target_metric="both",
            max_tests=args.max_tests,
            include_memory_options=args.include_memory_options,
            jobs=args.jobs,
            image_type=args.image_type,
            log_options=args.log_options,
            build_dir=build_dir,
            ec_dir=EC_BASE,
        )

        report_data = optimizer.run()

        # Format output
        if args.format == "json":
            output_content = json.dumps(report_data, indent=2)
        elif args.format == "text":
            output_content = generate_text_report(report_data)
        else:
            output_content = generate_markdown_report(report_data)

        # Print to stdout if no output file specified
        if not output_path:
            print("\n" + output_content)
        else:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, "w", encoding="utf-8") as f:
                f.write(output_content)
            logging.info("Saved report to %s", output_path)

    except Exception as err:  # pylint: disable=broad-exception-caught
        logging.error(
            "Fatal error during optimization: %s", err, exc_info=args.verbose
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
