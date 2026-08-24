#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import subprocess


class ServoDriver:
    """Wrapper for servod API and dut-control execution."""

    def __init__(self, port="9999", dut_ip="", dry_run=False):
        self.port = port
        self.dut_ip = dut_ip
        self.dry_run = dry_run
        self._logger = logging.getLogger(self.__class__.__name__)

    def _run_cmd(self, args):
        if self.dry_run:
            self._logger.info(f"DRY RUN: {' '.join(args)}")
            return "dry_run_output"

        try:
            result = subprocess.run(
                args,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            return result.stdout.strip()
        except subprocess.CalledProcessError as e:
            self._logger.error(
                f"dut-control command failed: {' '.join(args)}\nError: {e.stderr}"
            )
            raise

    def get(self, control):
        cmd = ["dut-control", "--", "-p", str(self.port)]
        if self.dut_ip:
            cmd.extend(["--host", self.dut_ip])
        cmd.append(control)
        out = self._run_cmd(cmd)
        if self.dry_run:
            return "unknown"
        # dut-control output is typically 'control:value'
        if ":" in out:
            return out.split(":", 1)[1].strip()
        return out

    def set(self, control, value):
        cmd = ["dut-control", "--", "-p", str(self.port)]
        if self.dut_ip:
            cmd.extend(["--host", self.dut_ip])
        cmd.append(f"{control}:{value}")
        self._run_cmd(cmd)
