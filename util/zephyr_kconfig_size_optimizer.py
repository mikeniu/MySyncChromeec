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

# pylint: disable=line-too-long,too-many-lines,broad-exception-caught,too-many-return-statements,subprocess-run-check,unused-variable,import-outside-toplevel,no-else-return,unbalanced-tuple-unpacking,missing-function-docstring,unused-import,import-error

import argparse
import json
import logging
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Set, Tuple


# Locate EC_BASE and ZEPHYR_BASE
def find_ec_base() -> pathlib.Path:
    candidates = [
        pathlib.Path(__file__).parent.parent.resolve(),
        pathlib.Path.cwd().resolve(),
        pathlib.Path.home() / "cros-trees" / "main" / "src" / "platform" / "ec",
        pathlib.Path.home() / "chromiumos" / "src" / "platform" / "ec",
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

try:
    import kconfiglib

    HAVE_KCONFIGLIB = True
except ImportError:
    HAVE_KCONFIGLIB = False


# ==============================================================================
# Known High-Impact & Optional Kconfigs Database
# ==============================================================================

HIGH_IMPACT_KCONFIGS: Dict[str, Dict[str, Any]] = {
    "LOG": {"action": "CONFIG_LOG=n", "test_values": ["n"]},
    "PRINTK": {"action": "CONFIG_PRINTK=n", "test_values": ["n"]},
    "SHELL": {"action": "CONFIG_SHELL=n", "test_values": ["n"]},
    "EC_CONSOLE": {"action": "CONFIG_EC_CONSOLE=n", "test_values": ["n"]},
    "ASSERT": {"action": "CONFIG_ASSERT=n", "test_values": ["n"]},
    "ASSERT_LEVEL": {
        "action": "CONFIG_ASSERT_LEVEL=1",
        "test_values": ["1", "0"],
    },
    "THREAD_NAME": {"action": "CONFIG_THREAD_NAME=n", "test_values": ["n"]},
    "THREAD_MONITOR": {
        "action": "CONFIG_THREAD_MONITOR=n",
        "test_values": ["n"],
    },
    "THREAD_STACK_INFO": {
        "action": "CONFIG_THREAD_STACK_INFO=n",
        "test_values": ["n"],
    },
    "BOOT_BANNER": {"action": "CONFIG_BOOT_BANNER=n", "test_values": ["n"]},
    "TRACING": {"action": "CONFIG_TRACING=n", "test_values": ["n"]},
    "COVERAGE": {"action": "CONFIG_COVERAGE=n", "test_values": ["n"]},
    "CBPRINTF_COMPLETE": {
        "action": "CONFIG_CBPRINTF_NANO=y",
        "test_values": ["n"],
    },
    "CBPRINTF_FP_SUPPORT": {
        "action": "CONFIG_CBPRINTF_FP_SUPPORT=n",
        "test_values": ["n"],
    },
    "PM_DEVICE": {"action": "CONFIG_PM_DEVICE=n", "test_values": ["n"]},
    "REBOOT": {"action": "CONFIG_REBOOT=n", "test_values": ["n"]},
    "SYS_HEAP_VALIDATE_STEPS": {
        "action": "CONFIG_SYS_HEAP_VALIDATE_STEPS=0",
        "test_values": ["0"],
    },
    "PLATFORM_EC_ACCEL": {
        "action": "CONFIG_PLATFORM_EC_ACCEL=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_BATTERY": {
        "action": "CONFIG_PLATFORM_EC_BATTERY=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_CHARGER": {
        "action": "CONFIG_PLATFORM_EC_CHARGER=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_LED": {
        "action": "CONFIG_PLATFORM_EC_LED=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_PANIC": {
        "action": "CONFIG_PLATFORM_EC_PANIC=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_SWITCH": {
        "action": "CONFIG_PLATFORM_EC_SWITCH=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_SYSTEM_UNSAFE_HOST_COMMANDS": {
        "action": "CONFIG_PLATFORM_EC_SYSTEM_UNSAFE_HOST_COMMANDS=n",
        "test_values": ["n"],
    },
    "PLATFORM_EC_PRESERVED_END_OF_RAM": {
        "action": "CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM=n",
        "test_values": ["n"],
    },
}

# Optional Memory and Stack Tuning Kconfigs (Requires --include-memory-options)
MEMORY_TUNING_KCONFIGS: Dict[str, Dict[str, Any]] = {
    "MAIN_STACK_SIZE": {
        "action": "CONFIG_MAIN_STACK_SIZE=1024",
        "test_values": ["1024", "2048"],
    },
    "ISR_STACK_SIZE": {
        "action": "CONFIG_ISR_STACK_SIZE=1024",
        "test_values": ["1024", "1536"],
    },
    "SYSTEM_WORKQUEUE_STACK_SIZE": {
        "action": "CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024",
        "test_values": ["1024", "1536"],
    },
    "HEAP_MEM_POOL_SIZE": {
        "action": "CONFIG_HEAP_MEM_POOL_SIZE=0",
        "test_values": ["0", "256", "512"],
    },
    "IDLE_STACK_SIZE": {
        "action": "CONFIG_IDLE_STACK_SIZE=256",
        "test_values": ["256", "384"],
    },
    "LOG_BUFFER_SIZE": {
        "action": "CONFIG_LOG_BUFFER_SIZE=512",
        "test_values": ["512", "1024"],
    },
    "SHELL_BACKEND_RTT_BUFFER_SIZE": {
        "action": "CONFIG_SHELL_BACKEND_RTT_BUFFER_SIZE=256",
        "test_values": ["256"],
    },
    "SHELL_CMD_BUFF_SIZE": {
        "action": "CONFIG_SHELL_CMD_BUFF_SIZE=64",
        "test_values": ["64", "128"],
    },
}

# Patterns for Mandatory Kconfigs that MUST NOT be disabled or tested
MANDATORY_KCONFIG_PATTERNS = [
    r"^ARCH$",
    r"^SOC$",
    r"^CPU$",
    r"^ARM$",
    r"^CORTEX",
    r"^SOC_SERIES",
    r"^SOC_FAMILY",
    r"^MULTITHREADING$",
    r"^NUM_IRQS$",
    r"^SYS_CLOCK_EXISTS$",
    r"^GENERIC_INTERRUPTS$",
    r"^SW_ISR_TABLE$",
    r"^ISR_TABLES$",
    r"^HAS_DTS$",
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


def is_mandatory_kconfig(sym_name: str, kconf=None) -> bool:
    """Check if a Kconfig symbol is mandatory for core system execution or non-configurable."""
    for pattern in MANDATORY_KCONFIG_PATTERNS:
        if re.search(pattern, sym_name):
            return True
    if kconf and sym_name in kconf.syms:
        sym = kconf.syms[sym_name]
        if not sym.nodes or not sym.nodes[0].prompt:
            return True
    return False


def parse_memory_usage(output: str) -> Tuple[int, int]:
    """Parse Flash/ROM and RAM size in bytes from Ninja/LD build output."""
    flash_bytes = 0
    ram_bytes = 0
    for line in output.splitlines():
        line = line.strip()
        # Match lines like: FLASH_RO: 278060 B, SRAM: 41852 B or ROM: 331740 B 1 MB 31.64% or RAM: 57740 B
        m = re.search(
            r"\b(FLASH[A-Z_]*|ROM[A-Z_]*|RAM|SRAM):\s*(\d+)\s*([KMGT]?B)",
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
                if flash_bytes == 0:
                    flash_bytes = bytes_val
    return flash_bytes, ram_bytes


def extract_failure_reason(error_log: str) -> str:
    """Extract a human-readable failure reason from build/Kconfig error output."""
    if not error_log:
        return "Build compilation failed"
    m_sel = re.search(
        r"is currently being y-selected by the following symbols:\s*\n\s*-\s*([A-Z0-9_]+)",
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
        r"attempt to assign the value '.*?' to the undefined symbol ([A-Z0-9_]+)",
        error_log,
    )
    if m_undef:
        return f"Undefined symbol CONFIG_{m_undef.group(1)}"
    m_ref = re.search(r"undefined reference to [`']([a-z0-9_]+)", error_log)
    if m_ref:
        return f"Linker error: undefined reference to {m_ref.group(1)}"
    m_gcc = re.search(r"error:\s+([^\n]+)", error_log)
    if m_gcc:
        return f"Compiler error: {m_gcc.group(1).strip()}"
    return "Build compilation failed"


# ==============================================================================
# Dependency & Side-Effect Resolver Class
# ==============================================================================


class KconfigDependencyResolver:
    """Manages Kconfig tree parsing and side-effect dependency resolution using kconfiglib."""

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
        self._load_kconfig_tree()

    def _load_kconfig_tree(self):
        """Initialize kconfiglib with the build environment and load .config."""
        if not HAVE_KCONFIGLIB:
            logging.warning(
                "kconfiglib not available. Dependency side-effects will be limited."
            )
            return

        build_kconfig_dir = self.build_dir / "Kconfig"
        if (
            not build_kconfig_dir.exists()
            and (self.build_dir / "build-rw" / "Kconfig").exists()
        ):
            build_kconfig_dir = self.build_dir / "build-rw" / "Kconfig"

        sys.path.insert(0, str(self.zephyr_base / "scripts" / "kconfig"))
        try:
            import kconfiglib  # pylint: disable=redefined-outer-name,import-outside-toplevel
        except ImportError:
            pass

        os.environ["KCONFIG_BINARY_DIR"] = str(build_kconfig_dir)
        os.environ["ZEPHYR_BASE"] = str(self.zephyr_base)
        os.environ["srctree"] = str(self.zephyr_base)
        os.environ["ARCH"] = "arm"
        os.environ["BOARD_DIR"] = str(
            self.zephyr_base / "boards" / "arm" / "null"
        )
        os.environ["ZEPHYR_CMSIS_6_KCONFIG"] = "/dev/null"
        os.environ["ZEPHYR_EC_KCONFIG"] = str(self.ec_base / "Kconfig")

        env_file = build_kconfig_dir / "kconfig_module_dirs.env"
        if env_file.exists():
            with open(env_file, "r", encoding="utf-8") as f:
                for line in f:
                    if "=" in line:
                        k, v = line.strip().split("=", 1)
                        os.environ[k] = v

        try:
            root_kconfig = self.zephyr_base / "Kconfig"
            if not root_kconfig.exists():
                logging.warning(
                    "Root Kconfig file not found at %s", root_kconfig
                )
                return
            self.kconf = kconfiglib.Kconfig(str(root_kconfig))
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
        except Exception as err:
            logging.warning(
                "Could not load Kconfig tree with kconfiglib: %s", err
            )
            self.kconf = None

    def get_side_effects(
        self, sym_name: str, new_val: str = "n"
    ) -> List[Tuple[str, str, str]]:
        """Calculate side effects (other Kconfigs that change) when modifying sym_name."""
        if not self.kconf or sym_name not in self.kconf.syms:
            return []

        orig_state = {}
        for name, sym in self.kconf.syms.items():
            if sym.str_value != "n":
                orig_state[name] = sym.str_value

        target_sym = self.kconf.syms[sym_name]
        try:
            target_sym.set_value(new_val)
        except Exception:
            return []

        changes = []
        for name, orig_val in orig_state.items():
            if name == sym_name:
                continue
            curr_val = self.kconf.syms[name].str_value
            if curr_val != orig_val:
                changes.append((name, orig_val, curr_val))

        # Reset symbol back
        try:
            default_reset = "0" if target_sym.type == kconfiglib.INT else "y"
            reset_val = orig_state.get(sym_name, default_reset)
            if reset_val != "":
                target_sym.set_value(reset_val)
        except Exception:
            pass

        return changes

    def is_selected_by_others(self, sym_name: str) -> List[str]:
        """Check if sym_name is selected by other active options."""
        if not self.kconf or sym_name not in self.kconf.syms:
            return []
        sym = self.kconf.syms[sym_name]
        if not hasattr(sym, "rev_dep") or not sym.rev_dep:
            return []
        try:
            import kconfiglib  # pylint: disable=redefined-outer-name,import-outside-toplevel

            return [
                x.name
                for x in kconfiglib.expr_items(sym.rev_dep)
                if x.name in self.kconf.syms
                and self.kconf.syms[x.name].str_value == "y"
            ]
        except Exception:
            return []

    def find_newly_exposed_options(
        self, sym_name: str, new_val: str
    ) -> List[str]:
        """Find Kconfig options that become visible/configurable when sym_name is set to new_val."""
        if not self.kconf or sym_name not in self.kconf.syms:
            return []

        orig_visible = set()
        for name, sym in self.kconf.syms.items():
            if (
                sym.nodes
                and sym.nodes[0].prompt
                and getattr(sym, "visibility", 0) > 0
            ):
                orig_visible.add(name)

        target_sym = self.kconf.syms[sym_name]
        orig_val = target_sym.str_value
        try:
            target_sym.set_value(new_val)
        except Exception:
            return []

        new_visible = set()
        for name, sym in self.kconf.syms.items():
            if (
                sym.nodes
                and sym.nodes[0].prompt
                and getattr(sym, "visibility", 0) > 0
            ):
                new_visible.add(name)

        # Reset symbol back
        try:
            target_sym.set_value(orig_val)
        except Exception:
            pass

        exposed = list(new_visible - orig_visible)
        return sorted(exposed)

    def get_symbol_candidates(
        self, sym_name: str, include_memory_options: bool = False
    ) -> List[Tuple[str, str, str]]:
        """
        Get valid candidate value variations for sym_name based on type:
        - Bool / Tristate: toggle y <-> n
        - Choice: test setting alternative choices to 'y'
        - Int / Log Levels: test lower levels (e.g. 0 to N-1) for _LEVEL options, or memory sizes if include_memory_options is True
        Returns list of (option_name, new_val, action_string).
        """
        if not self.kconf or sym_name not in self.kconf.syms:
            return []

        sym = self.kconf.syms[sym_name]
        if not sym.nodes or not sym.nodes[0].prompt:
            return []

        cur_val = sym.str_value
        results = []

        # Handle Choice Symbols
        if sym.choice and sym.choice.syms:
            for alt in sym.choice.syms:
                if alt.name != sym_name and getattr(alt, "visibility", 0) > 0:
                    results.append((alt.name, "y", f"CONFIG_{alt.name}=y"))
            return results

        # Handle Bool / Tristate Symbols
        if sym.type in (kconfiglib.BOOL, kconfiglib.TRISTATE):
            target_val = "n" if cur_val in ["y", "1"] else "y"
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
            if sym_name in MEMORY_TUNING_KCONFIGS and include_memory_options:
                test_vals = MEMORY_TUNING_KCONFIGS[sym_name]["test_values"]
                for tv in test_vals:
                    if tv != cur_val:
                        results.append(
                            (sym_name, tv, f"CONFIG_{sym_name}={tv}")
                        )
                return results

            # Log / Assert / Debug level integers: test all values starting from (cur_val - 1) down to 0
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
    """Manages Zmake configuration and Ninja build execution for size measurements."""

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
            subprocess.run(
                ["rm", "-rf", str(self.project_build_dir)], check=False
            )
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
        """Run ninja build and measure Flash and RAM usage for target image_type (rw, ro, or both)."""
        start_t = time.time()
        target_dir = (
            self.build_rw_dir if self.image_type == "rw" else self.build_ro_dir
        )
        cmd = ["ninja", "-C", str(target_dir)]
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        elapsed = time.time() - start_t
        if proc.returncode != 0:
            return None, None, False, elapsed, proc.stdout
        flash, ram = parse_memory_usage(proc.stdout)
        return flash, ram, True, elapsed, proc.stdout

    def test_kconfig_changes(
        self, changes: List[str]
    ) -> Tuple[Optional[int], Optional[int], bool, float, str]:
        """Test a list of Kconfig lines by appending to kconfig-rw.conf or kconfig-ro.conf and building."""
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

        success = False
        log_output = ""
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
            if not success:
                # Restore build directory to baseline state after failure
                self.build_and_measure()


# ==============================================================================
# Kconfig Optimizer & Report Generator
# ==============================================================================


class ZephyrKconfigOptimizer:
    """Orchestrates Kconfig size impact search and generates reports."""

    def __init__(
        self,
        target: str,
        aggressiveness: str = "quick",
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
        self.aggressiveness = aggressiveness.lower()
        self.target_metric = target_metric.lower()
        self.max_tests = max_tests
        self.include_memory_options = include_memory_options
        self.jobs = max(1, jobs)
        self.image_type = image_type.lower()
        self.log_options = log_options.lower()
        self.ec_base = ec_dir or EC_BASE
        self.build_root = build_dir or pathlib.Path(f"/tmp/zmake_opt_{target}")
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
            "Mode: %s | Image Type: %s | Parallel Jobs: %d | Include Memory: %s",
            self.aggressiveness,
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
            "Baseline Size (%s): Flash = %d bytes (%.2f KB), RAM = %d bytes (%.2f KB) [Took %.2fs]",
            self.image_type.upper(),
            self.baseline_flash,
            self.baseline_flash / 1024.0,
            self.baseline_ram,
            self.baseline_ram / 1024.0,
            elapsed,
        )

        # Step 3: Initialize Dependency Resolver
        target_kconf_dir = (
            self.runner.build_rw_dir
            if self.image_type == "rw"
            else self.runner.build_ro_dir
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
        """Collect candidate Kconfigs based on search mode (quick vs full)."""
        candidates = []
        seen_actions = set()
        seen_options = set()
        active_configs = self._get_active_configs()

        # Target Database: Include MEMORY_TUNING_KCONFIGS only if --include-memory-options is enabled
        target_db = dict(HIGH_IMPACT_KCONFIGS)
        if self.include_memory_options:
            target_db.update(MEMORY_TUNING_KCONFIGS)

        # Quick Mode: Known High-Impact Options
        for sym_name, meta in target_db.items():
            config_key = f"CONFIG_{sym_name}"
            actual_val = "n"
            if (
                self.resolver
                and self.resolver.kconf
                and sym_name in self.resolver.kconf.syms
            ):
                actual_val = self.resolver.kconf.syms[sym_name].str_value
            else:
                actual_val = active_configs.get(
                    config_key, "y" if meta["test_values"][0] == "n" else "n"
                )

            for t_val in meta["test_values"]:
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

        if self.aggressiveness in ["quick", "1"]:
            return candidates

        # Full / Recursive Mode: Discover active non-mandatory Kconfigs in .config
        kconf_ref = self.resolver.kconf if self.resolver else None
        for config_key, val in active_configs.items():
            sym_name = config_key.replace("CONFIG_", "")
            if (
                sym_name in HIGH_IMPACT_KCONFIGS
                or sym_name in MEMORY_TUNING_KCONFIGS
                or is_mandatory_kconfig(sym_name, kconf_ref)
            ):
                continue

            # Check if prompt-bearing / configurable in kconfiglib
            if (
                self.resolver
                and self.resolver.kconf
                and sym_name in self.resolver.kconf.syms
            ):
                typed_cands = self.resolver.get_symbol_candidates(
                    sym_name, self.include_memory_options
                )
                for opt_name, t_val, act in typed_cands:
                    if act not in seen_actions:
                        seen_actions.add(act)
                        seen_options.add(opt_name)
                        cand_cur_val = val
                        if opt_name in self.resolver.kconf.syms:
                            cand_cur_val = self.resolver.kconf.syms[
                                opt_name
                            ].str_value
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

        # Recursive Mode: Find newly exposed options enabled by toggling parent features
        if self.aggressiveness in ["recursive", "3"] and self.resolver:
            logging.info(
                "Running recursive dependency exploration to discover hidden Kconfigs..."
            )
            exposed_candidates = []

            explore_set = (
                candidates[: self.max_tests]
                if self.max_tests > 0
                else candidates[:]
            )
            for cand in explore_set:
                s_name = cand["name"]
                # Check for exposed options if toggling to 'y' or 'n'
                for test_v in ["y", "n"]:
                    newly_exposed = self.resolver.find_newly_exposed_options(
                        s_name, test_v
                    )
                    for exp_sym in newly_exposed:
                        if (
                            exp_sym not in seen_options
                            and not is_mandatory_kconfig(exp_sym)
                        ):
                            exp_cur_val = active_configs.get(
                                f"CONFIG_{exp_sym}", "n"
                            )
                            exp_target_val = (
                                "n" if exp_cur_val in ["y", "1"] else "0"
                            )
                            act = f"CONFIG_{exp_sym}={exp_target_val}"
                            if act not in seen_actions:
                                seen_actions.add(act)
                                seen_options.add(exp_sym)
                                exposed_candidates.append(
                                    {
                                        "name": exp_sym,
                                        "option": exp_sym,
                                        "current_val": exp_cur_val,
                                        "new_val": exp_target_val,
                                        "action": act,
                                        "dependency_path": f"CONFIG_{s_name}={test_v} -> CONFIG_{exp_sym}",
                                    }
                                )

            logging.info(
                "Discovered %d newly exposed hidden Kconfig options!",
                len(exposed_candidates),
            )
            candidates.extend(exposed_candidates)

        # Apply Log Options Filtering (include, exclude, only)
        if self.log_options == "exclude":
            candidates = [
                c for c in candidates if not is_log_kconfig(c["name"])
            ]
            logging.info(
                "Applied --log-options=exclude: %d non-log candidates remain",
                len(candidates),
            )
        elif self.log_options == "only":
            candidates = [c for c in candidates if is_log_kconfig(c["name"])]
            logging.info(
                "Applied --log-options=only: %d log-related candidates remain",
                len(candidates),
            )

        return candidates

    def _get_active_configs(self) -> Dict[str, str]:
        """Read active configs from target build dir .config."""
        target_dir = (
            self.runner.build_rw_dir
            if self.image_type == "rw"
            else self.runner.build_ro_dir
        )
        config_path = target_dir / "zephyr" / ".config"
        active = {}
        if not config_path.exists():
            return active

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
        """Parse build error log to find missing dependency symbols and suggest repair actions."""
        repair_actions = []
        seen_syms = set()

        # 1. Look for Kconfig 'is currently being y-selected by the following symbols' in log
        for match in re.finditer(
            r"is currently being y-selected by the following symbols:\s*\n\s*-\s*([A-Z0-9_]+)",
            error_log,
        ):
            sel_sym = match.group(1)
            if sel_sym not in seen_syms and not is_mandatory_kconfig(sel_sym):
                seen_syms.add(sel_sym)
                repair_actions.append((sel_sym, "n", f"CONFIG_{sel_sym}=n"))

        # 2. Look for Kconfig warning dependencies in log: e.g. 'FOO has direct dependencies BAR with value n'
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

        # 3. Look for undefined references to cros_ec or zephyr drivers: e.g. 'undefined reference to `cros_ec_accel_...'
        for match in re.finditer(
            r"undefined reference to [`']cros_ec_([a-z0-9_]+)", error_log
        ):
            sub_name = match.group(1).upper()
            sym_candidate = f"PLATFORM_EC_{sub_name}"
            if (
                sym_candidate not in seen_syms
                and self.resolver
                and self.resolver.kconf
                and sym_candidate in self.resolver.kconf.syms
            ):
                seen_syms.add(sym_candidate)
                repair_actions.append(
                    (sym_candidate, "n", f"CONFIG_{sym_candidate}=n")
                )

        # 4. Look for undeclared CONFIG_ symbol errors in GCC output: e.g. 'error: 'CONFIG_FOO' undeclared'
        for match in re.finditer(
            r"['`]CONFIG_([A-Z0-9_]+)['`]\s+undeclared", error_log
        ):
            sym_candidate = match.group(1)
            if sym_candidate not in seen_syms and not is_mandatory_kconfig(
                sym_candidate
            ):
                seen_syms.add(sym_candidate)
                repair_actions.append(
                    (sym_candidate, "n", f"CONFIG_{sym_candidate}=n")
                )

        return repair_actions

    def _evaluate_candidates(self, candidates: List[Dict[str, Any]]):
        """Test each candidate Kconfig option and record size impact and side effects with optional parallel workers."""
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
                if item.get("dependency_path"):
                    side_effects.append(f"[Path: {item['dependency_path']}]")

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
                    self.runner.test_kconfig_changes([action])
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
                        " -> Flash Savings: %+d B (%.2f%%) | RAM Savings: %+d B (%.2f%%) | Time: %.2fs",
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
                            "test_time": elapsed,
                        }
                    )
                else:
                    # Attempt Auto-Repair (Capped at 1 compound attempt)
                    repair_acts = self._find_missing_dep_symbols(log_out)
                    repaired_success = False
                    if repair_acts:
                        repair_sym, repair_val, repair_act = repair_acts[0]
                        logging.info(
                            " -> Attempting Auto-Repair for %s: Appending %s...",
                            opt_name,
                            repair_act,
                        )
                        r_flash, r_ram, r_succ, r_elapsed, _ = (
                            self.runner.test_kconfig_changes(
                                [action, repair_act]
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
                                " -> Auto-Repair SUCCESS! %s + %s: Flash %+dB | RAM %+dB [Took %.2fs]",
                                action,
                                repair_act,
                                d_flash,
                                d_ram,
                                r_elapsed,
                            )
                            self.results.append(
                                {
                                    "name": sym_name,
                                    "option": opt_name,
                                    "current_val": cur_val,
                                    "new_val": f"{new_val} (Auto-Repaired with {repair_sym}={repair_val})",
                                    "action": f"{action} && {repair_act}",
                                    "success": True,
                                    "flash_savings": d_flash,
                                    "ram_savings": d_ram,
                                    "flash_pct": pct_flash,
                                    "ram_pct": pct_ram,
                                    "side_effects": side_effects
                                    + [f"[Auto-Repair: {repair_act}]"],
                                    "test_time": elapsed + r_elapsed,
                                    "auto_repaired": True,
                                }
                            )
                            repaired_success = True

                    if not repaired_success:
                        logging.info(
                            " -> Build FAILED with %s (Time: %.2fs)",
                            action,
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
                                "test_time": elapsed,
                            }
                        )
        else:
            # Parallel evaluation with N worker build directories
            import concurrent.futures
            import queue

            logging.info(
                "Initializing %d parallel worker build directories...",
                self.jobs,
            )
            worker_runners = []
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
                opt_name = item["option"]
                cur_val = item["current_val"]
                new_val = item["new_val"]
                action = item["action"]

                w_runner = runner_queue.get()
                try:
                    # Side effects
                    side_effects = []
                    if item.get("dependency_path"):
                        side_effects.append(
                            f"[Path: {item['dependency_path']}]"
                        )

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
                        w_runner.test_kconfig_changes([action])
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

                        log_repairs = self._find_missing_dep_symbols(log_out)
                        candidate_repairs.extend(log_repairs)

                        for (
                            repair_sym,
                            repair_val,
                            repair_act,
                        ) in candidate_repairs:
                            orig_repair_val = "y"
                            if (
                                self.resolver
                                and self.resolver.kconf
                                and repair_sym in self.resolver.kconf.syms
                            ):
                                orig_repair_val = self.resolver.kconf.syms[
                                    repair_sym
                                ].str_value
                            r_flash, r_ram, r_succ, r_elapsed, _ = (
                                w_runner.test_kconfig_changes(
                                    [action, repair_act]
                                )
                            )
                            if (
                                r_succ
                                and r_flash is not None
                                and r_ram is not None
                            ):
                                dependent_changes.append(
                                    f"CONFIG_{repair_sym} ({orig_repair_val} -> {repair_val})"
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
                                    f"{new_val} (Auto-Repaired with {repair_sym}={repair_val})",
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

                        repair_tag = " [AUTO-REPAIRED]" if is_repaired else ""
                        logging.info(
                            "[%d/%d] %s (%s -> %s)%s: Flash %+-6dB (%.1f%%) | RAM %+-6dB (%.1f%%) [Took %.1fs]",
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
                            "[%d/%d] %s (%s -> %s): Build FAILED (%s) [Took %.1fs]",
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
                                "failure_reason": reason_str,
                                "test_time": elapsed,
                            }
                        )

        # Sort results by size savings based on target_metric
        def sort_key(res):
            if not res["success"]:
                return -99999999
            if self.target_metric == "flash":
                return res["flash_savings"]
            elif self.target_metric == "ram":
                return res["ram_savings"]
            else:
                return res["flash_savings"] + (
                    res["ram_savings"] * 4
                )  # Weight RAM higher

        self.results.sort(key=sort_key, reverse=True)

    def _evaluate_cumulative_plan(self) -> List[str]:
        """Test combining top non-conflicting recommendations together."""
        logging.info("Evaluating cumulative multi-option optimization plan...")
        top_safe_actions = []
        for res in self.results:
            if res["success"]:
                if res["flash_savings"] > 0 or res["ram_savings"] > 0:
                    top_safe_actions.append(res["action"])

        if not top_safe_actions:
            return []

        flash, ram, success, elapsed = self.runner.test_kconfig_changes(
            top_safe_actions
        )
        if success and flash is not None and ram is not None:
            d_flash = self.baseline_flash - flash
            d_ram = self.baseline_ram - ram
            logging.info(
                "Cumulative Plan Success! Total Flash Savings: %d B (%.2f KB), RAM Savings: %d B (%.2f KB)",
                d_flash,
                d_flash / 1024.0,
                d_ram,
                d_ram / 1024.0,
            )
            return top_safe_actions
        else:
            logging.warning(
                "Cumulative combination test failed due to option conflicts."
            )
            return []


# ==============================================================================
# Report Generators
# ==============================================================================


def generate_markdown_report(report_data: Dict[str, Any]) -> str:
    """Generate Markdown report summarizing size impact and side effects."""
    target = report_data["target"]
    baseline = report_data["baseline"]
    results = report_data["results"]
    cumulative_plan = report_data.get("cumulative_plan", [])

    lines = []
    lines.append(f"# Zephyr EC Kconfig Size Optimization Report: `{target}`")
    lines.append("")
    lines.append("## Executive Summary")
    lines.append("")
    lines.append(f"- **Target Build**: `{target}`")
    lines.append(
        f"- **Baseline Flash Size**: {baseline['flash']:,} Bytes ({baseline['flash']/1024.0:.2f} KB)"
    )
    lines.append(
        f"- **Baseline RAM Size**: {baseline['ram']:,} Bytes ({baseline['ram']/1024.0:.2f} KB)"
    )
    lines.append(f"- **Total Kconfigs Evaluated**: {len(results)}")
    lines.append("")

    # Recommendations Table
    lines.append("## Kconfig Size Impact Table")
    lines.append("")
    lines.append(
        "| Option | Current Value | New Value | Flash Savings | RAM Savings | Side Effects |"
    )
    lines.append("| :--- | :--- | :--- | :--- | :--- | :--- |")

    for res in results:
        if not res["success"]:
            continue
        opt_str = f"`{res.get('option', res['name'])}`"
        cur_val = f"`{res.get('current_val', 'y')}`"
        new_val = f"`{res.get('new_val', 'n')}`"
        flash_str = (
            f"**-{res['flash_savings']:,} B** ({res['flash_pct']:.1f}%)"
            if res["flash_savings"] > 0
            else f"+{-res['flash_savings']:,} B"
        )
        ram_str = (
            f"**-{res['ram_savings']:,} B** ({res['ram_pct']:.1f}%)"
            if res["ram_savings"] > 0
            else f"+{-res['ram_savings']:,} B"
        )
        side_effects = (
            ", ".join(res["side_effects"]) if res["side_effects"] else ""
        )
        lines.append(
            f"| {opt_str} | {cur_val} | {new_val} | {flash_str} | {ram_str} | {side_effects} |"
        )

    lines.append("")

    # Failed / Incompatible Options section if any
    failed_opts = [r for r in results if not r["success"]]
    if failed_opts:
        lines.append("## Incompatible / Failed Kconfig Changes")
        lines.append("")
        lines.append(
            "The following Kconfigs failed to compile when modified individually:"
        )
        lines.append("")
        for f_opt in failed_opts:
            opt_str = f_opt.get("option", f_opt["name"])
            reason = f_opt.get("failure_reason", "Build compilation failed.")
            lines.append(
                f"- `{opt_str}` (`{f_opt.get('current_val', 'y')}` -> `{f_opt.get('new_val', 'n')}`): {reason}"
            )
        lines.append("")

    # Recommended Fragment
    if cumulative_plan:
        lines.append(
            "## Verified Cumulative Optimization Fragment (`optimizations.conf`)"
        )
        lines.append("")
        lines.append("```kconfig")
        lines.append("# Recommended size optimization options for Zephyr EC")
        for act in cumulative_plan:
            lines.append(act)
        lines.append("```")
        lines.append("")

    return "\n".join(lines)


def generate_text_report(report_data: Dict[str, Any]) -> str:
    """Generate plain text report for terminal output."""
    target = report_data["target"]
    baseline = report_data["baseline"]
    results = report_data["results"]

    lines = []
    lines.append(
        "===================================================================================================="
    )
    lines.append(f" Zephyr EC Kconfig Size Optimization Report: {target}")
    lines.append(
        "===================================================================================================="
    )
    lines.append(
        f"Baseline Flash: {baseline['flash']:,} B ({baseline['flash']/1024.0:.2f} KB)"
    )
    lines.append(
        f"Baseline RAM:   {baseline['ram']:,} B ({baseline['ram']/1024.0:.2f} KB)"
    )
    lines.append(
        "----------------------------------------------------------------------------------------------------"
    )
    lines.append(
        f"{'Option':<32} | {'Current':<8} | {'New':<8} | {'Flash Savings':<14} | {'RAM Savings':<14} | {'Side Effects'}"
    )
    lines.append(
        "----------------------------------------------------------------------------------------------------"
    )

    for res in results:
        if not res["success"]:
            continue
        opt_str = res.get("option", res["name"])
        cur_val = str(res.get("current_val", "y"))
        new_val = str(res.get("new_val", "n"))
        flash_str = (
            f"-{res['flash_savings']:,} B"
            if res["flash_savings"] >= 0
            else f"+{-res['flash_savings']:,} B"
        )
        ram_str = (
            f"-{res['ram_savings']:,} B"
            if res["ram_savings"] >= 0
            else f"+{-res['ram_savings']:,} B"
        )
        side_effects = (
            ", ".join(res["side_effects"]) if res["side_effects"] else ""
        )
        lines.append(
            f"{opt_str:<32} | {cur_val:<8} | {new_val:<8} | {flash_str:<14} | {ram_str:<14} | {side_effects}"
        )

    lines.append(
        "===================================================================================================="
    )
    return "\n".join(lines)


# ==============================================================================
# Main Entry Point
# ==============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Zephyr EC Kconfig Size Optimizer & Impact Analyzer",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "target",
        help="Build target name (e.g. 'craask', 'nissa', 'brox', 'tell.nissa', etc.)",
    )
    parser.add_argument(
        "-m",
        "--mode",
        choices=["quick", "full", "recursive"],
        default="full",
        help=(
            "Search mode:\n"
            "  full (default): Evaluate all active non-mandatory Kconfig options in .config\n"
            "  quick: Evaluate known high-impact options only\n"
            "  recursive: Full mode + discover & test hidden options exposed by toggling parent features"
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
        help="Include thread stack and memory pool size tuning (e.g. MAIN_STACK_SIZE, ISR_STACK_SIZE, HEAP_MEM_POOL_SIZE). Default: False (feature options only).",
    )
    parser.add_argument(
        "-b",
        "--image-type",
        choices=["rw", "ro", "both"],
        default="rw",
        help="Target firmware image to measure size impact on: 'rw' (default, Read-Write image), 'ro' (Read-Only image), or 'both'.",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=1,
        help="Number of parallel worker build directories to use for build evaluation (default: 1).",
    )
    parser.add_argument(
        "-L",
        "--log-options",
        choices=["include", "exclude", "only"],
        default="include",
        help="Log/debug level Kconfig options handling: 'include' (default, test log & feature options), 'exclude' (test features only, exclude logs), or 'only' (test log & debug options only).",
    )
    parser.add_argument(
        "-B",
        "--build-dir",
        type=pathlib.Path,
        help="Custom build directory (default: /tmp/zmake_opt_<target>).",
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
            aggressiveness=args.mode,
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

    except Exception as err:
        logging.error(
            "Fatal error during optimization: %s", err, exc_info=args.verbose
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
