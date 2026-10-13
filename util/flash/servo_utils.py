# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Servo communication wrapper utilities for EC flashing."""

import logging
import subprocess
from typing import List, Optional, Tuple


logger = logging.getLogger(__name__)


class ServoError(Exception):
    """Exception raised for errors communicating with servod."""


class ServoDriver:
    """Wrapper around dut-control to interact with a Servo instance."""

    def __init__(
        self,
        host: str = "localhost",
        port: int = 9999,
        dry_run: bool = False,
    ):
        self.host = host
        self.port = port
        self.dry_run = dry_run
        self._saved_state: List[Tuple[str, str]] = []
        self._effective_servo_type: Optional[str] = None
        self.prefix = ""
        self._detect_prefix()

    def _build_cmd(self, args: List[str]) -> List[str]:
        """Construct the base dut-control command."""
        cmd = ["dut-control", f"--port={self.port}"]
        if self.host and self.host != "localhost":
            cmd.append(f"--host={self.host}")
        cmd.extend(args)
        return cmd

    def _run_dut_control(
        self, args: List[str], check: bool = True
    ) -> subprocess.CompletedProcess:
        """Execute dut-control and return the completed process."""
        cmd = self._build_cmd(args)
        logger.debug("Executing: %s", " ".join(cmd))

        if self.dry_run and any(":" in arg for arg in args):
            logger.info("[DRY RUN] Would execute: %s", " ".join(cmd))
            return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

        try:
            return subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                encoding="utf-8",
                check=check,
            )
        except subprocess.CalledProcessError as err:
            logger.debug(
                "dut-control failed: %s (stderr: %s)",
                err,
                err.stderr.strip(),
            )
            raise ServoError(
                f"Failed to execute dut-control {args}: {err.stderr.strip()}"
            ) from err

    def _detect_prefix(self) -> None:
        """Determine if a control prefix is required for dual-v4 setups."""
        try:
            servo_type = self.get("servo_type")
            if "servo_v4" in servo_type and "_and_" in servo_type:
                active = self.get("active_dut_controller")
                self._effective_servo_type = f"servo_v4_with_{active}"
                second_dev = servo_type.split("_and_")[-1]
                if active == second_dev:
                    self.prefix = f"{active}."
                    logger.debug(
                        "Detected dual servo setup. Using prefix: %s",
                        self.prefix,
                    )
        except ServoError:
            logger.debug(
                "Could not auto-detect servo_type for prefixing. "
                "Assuming none."
            )

    def has_control(self, control: str) -> bool:
        """Check if the connected servo supports a specific control."""
        prefixed_ctrl = f"{self.prefix}{control}" if self.prefix else control
        res = self._run_dut_control(["-i", prefixed_ctrl], check=False)
        return res.returncode == 0

    def get(self, control: str, default: Optional[str] = None) -> str:
        """Query the value of a single servo control."""
        if control == "servo_type" and self._effective_servo_type is not None:
            return self._effective_servo_type
        prefixed_ctrl = f"{self.prefix}{control}" if self.prefix else control
        try:
            res = self._run_dut_control(["-o", prefixed_ctrl], check=True)
            return res.stdout.strip()
        except ServoError:
            if default is not None:
                return default
            raise

    def set(self, control: str, value: str) -> None:
        """Set the value of a single servo control."""
        prefixed_ctrl = f"{self.prefix}{control}" if self.prefix else control
        self._run_dut_control([f"{prefixed_ctrl}:{value}"], check=True)

    def set_many(self, controls: List[Tuple[str, str]]) -> None:
        """Set multiple servo controls in a single dut-control invocation."""
        args = []
        for ctrl, val in controls:
            prefixed_ctrl = f"{self.prefix}{ctrl}" if self.prefix else ctrl
            args.append(f"{prefixed_ctrl}:{val}")
        self._run_dut_control(args, check=True)

    def save_state(
        self, control: str, override_restore_val: Optional[str] = None
    ) -> None:
        """Save the current value of a control to be restored on exit."""
        if override_restore_val is not None:
            self._saved_state.insert(0, (control, override_restore_val))
        else:
            try:
                current_val = self.get(control)
                if current_val is not None:
                    self._saved_state.insert(0, (control, current_val))
            except ServoError as err:
                logger.warning(
                    "Could not save state for control %s: %s", control, err
                )

    def restore_state(self) -> None:
        """Restore all saved controls in LIFO order."""
        if not self._saved_state:
            return
        logger.info("Restoring servo settings...")
        for control, val in self._saved_state:
            try:
                self.set(control, val)
            except ServoError as err:
                logger.error(
                    "Failed to restore %s to %s: %s", control, val, err
                )
        self._saved_state.clear()
