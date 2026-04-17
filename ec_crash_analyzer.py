# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""EC Crash Analyzer Class

This script analyzes crash data from the EC as a result of
testing.
"""

import logging as logging_module
import os
import re
import subprocess
import tarfile
from typing import Any

from analyzers import result_analyzer


logger = logging_module.getLogger(__name__)

fw_name_quirks = {
    "trulo": ["orisa", "pujjocento", "pujjogatwin", "pujjolo", "trulo", "uldrenite"],
    "kukui": [
        "burnet",
        "cerise",
        "damu",
        "esche",
        "fennel",
        "fennel14",
        "jacuzzi",
        "juniper",
        "kappa",
        "stern",
        "willow",
    ],
    "brya": [
        "aurash",
        "brask",
        "bujia",
        "gaelin",
        "gladios",
        "kinox",
        "kuldax",
        "kulnex",
        "lisbon",
        "moli",
        "moxie",
    ],
}


class EcCrashAnalyzer(result_analyzer.ResultAnalyzer):
    """Analyzes the logs/status for EC crashes."""

    GCS_BUCKET_BASE = "gs://firmware-image-archive"
    ADDR2LINE_BIN = "addr2line"

    def __init__(self, cfg: result_analyzer.ResultAnalyzerArgs):
        super().__init__(
            name="ec_crash_analyzer",
            cfg=cfg,
            phase=result_analyzer.ResultAnalyzer.AnalysisPhase.CALCULATE,
            dependencies=["pd_analyzer"],
        )
        self.elf_cache_dir = os.path.join(self.log_dir, "elf_cache")
        # Due to case sensitivity and the register layouts these are currently
        #   unique enough. Should that change, further filtering may be
        #   required to determine the architecture
        self.panic_regex_patterns = [
            # Cortex
            r"r4\s*:\s*([0-9a-fA-F]+)",
            # NDS32
            r"R6\s+([0-9a-fA-F]+)",
            # RiscV
            r"S0\s+([0-9a-fA-F]+)",
        ]
        # These are in order by preference
        self.pc_regex_patterns = [
            # Cortex
            r"lr *: *([0-9a-fA-F]+)",
            r"pc *: *([0-9a-fA-F]+)",
            r"r5 *: *([0-9a-fA-F]+)",
            # NDS32
            r"IPC\s+([0-9a-fA-F]+)",
            # RiscV
            r"MEPC\s+([0-9a-fA-F]+)",
        ]

        self.error_codes = {
            "6660": "div-by-0",
            "6661": "stack-overflow",
            "6662": "pd-crash",
            "6663": "assert",
            "6664": "watchdog",
            "6665": "bad-rng",
            "6666": "pmic-fault",
            "6667": "exit",
            "6668": "watchdog-warn",
            "6800": "zephyr-fatal-error",
        }

    def _get_quirked_board(self, name: str | None) -> str | None:
        """Returns the quirked board name if it exists."""
        if not name:
            return None
        for quirk_board, names in fw_name_quirks.items():
            if name in names:
                return quirk_board
        return None

    def _get_board_from_fwid(self, fwid: Any) -> str | None:
        """Extracts the board name from the fwid."""
        if not fwid or fwid == "UNKNOWN":
            return None

        # If it's a set/list, take the first element
        if isinstance(fwid, (set, list)):
            if not fwid:
                return None
            fwid = next(iter(fwid))

        if not isinstance(fwid, str):
            return None

        # Split by '-' or '_' and take the first part
        # genesis_v2.0.4753-c3c7005743 -> genesis
        # jubilant-16080.121.0 -> jubilant
        return re.split(r"[-_]", fwid)[0]

    def _get_major_version(self, full_version: str) -> str | None:
        """Extracts '10139' from '10139.232.0'"""
        if not full_version or "." not in full_version:
            return None
        return full_version.split(".")[0]

    def _construct_gcs_path(
        self, board: str, version: str, filename_prefix: str
    ) -> str | None:
        """Constructs a candidate GCS path."""
        major = self._get_major_version(version)
        if not major:
            return None
        return (
            f"firmware-{board}-{major}.B/{version}/"
            f"{filename_prefix}.EC_elf.{version}.tar.bz2"
        )

    def _download_and_extract(
        self, board: str, model: str, version: str, fwid: Any = None
    ) -> tuple[str | None, str | None]:
        """Downloads and extracts the artifact ELFs."""
        if not version or version.lower() == "null":
            return None, None

        extract_dir = os.path.join(self.elf_cache_dir, board, model, version)

        # We need to determine the GCS path even if it's already extracted
        # to return it to the caller.
        gcs_path = None

        # Strategy 0: FWID based path
        fwid_board = self._get_board_from_fwid(fwid)
        path_fwid = None
        if fwid_board:
            path_fwid = self._construct_gcs_path(
                board, version, filename_prefix=fwid_board
            )

        # Strategy 1: Try BOARD based path
        path_primary = self._construct_gcs_path(
            board, version, filename_prefix=board
        )

        # Strategy 2: MODEL based path
        path_secondary = self._construct_gcs_path(
            board, version, filename_prefix=model
        )

        # Strategy 3: Quirked board mapping
        quirked_board = (
            self._get_quirked_board(fwid_board)
            or self._get_quirked_board(model)
            or self._get_quirked_board(board)
        )
        path_quirk = None
        if quirked_board and quirked_board != board:
            path_quirk = self._construct_gcs_path(
                quirked_board, version, filename_prefix=quirked_board
            )

        if os.path.exists(os.path.join(extract_dir, "ec.RO.elf")):
            # If it exists, we still need to know which path was successful
            # for reporting. For now, let's assume primary if it exists,
            # or secondary. This is a bit tricky without re-checking GCS.
            # However, the user wants the path. Let's try to verify which one
            # would be used.
            if path_fwid and self._blob_exists(path_fwid):
                return extract_dir, f"{self.GCS_BUCKET_BASE}/{path_fwid}"
            if path_primary and self._blob_exists(path_primary):
                return extract_dir, f"{self.GCS_BUCKET_BASE}/{path_primary}"
            if path_secondary and self._blob_exists(path_secondary):
                return extract_dir, f"{self.GCS_BUCKET_BASE}/{path_secondary}"
            if path_quirk and self._blob_exists(path_quirk):
                return extract_dir, f"{self.GCS_BUCKET_BASE}/{path_quirk}"
            return extract_dir, "unknown_gcs_path"

        os.makedirs(extract_dir, exist_ok=True)
        tar_filename = "artifact.tar.bz2"
        local_tar_path = os.path.join(extract_dir, tar_filename)

        success = False
        if path_fwid:
            success = self._attempt_download(path_fwid, local_tar_path)
            if success:
                gcs_path = f"{self.GCS_BUCKET_BASE}/{path_fwid}"

        if not success and path_primary:
            success = self._attempt_download(path_primary, local_tar_path)
            if success:
                gcs_path = f"{self.GCS_BUCKET_BASE}/{path_primary}"

        if not success and path_secondary:
            success = self._attempt_download(path_secondary, local_tar_path)
            if success:
                gcs_path = f"{self.GCS_BUCKET_BASE}/{path_secondary}"

        if not success and path_quirk:
            logger.info(
                "Attempting quirked board mapping: %s -> %s",
                board,
                quirked_board,
            )
            success = self._attempt_download(path_quirk, local_tar_path)
            if success:
                gcs_path = f"{self.GCS_BUCKET_BASE}/{path_quirk}"

        if not success:
            logger.warning(
                "Failed to download artifact for %s/%s %s",
                board,
                model,
                version,
            )
            try:
                os.rmdir(extract_dir)
            except OSError:
                pass
            return None, None

        # Extract
        try:
            targets = [
                "ec.RO.elf",
                "ec.RW.elf",
                "zephyr.ro.elf",
                "zephyr.rw.elf",
            ]
            with tarfile.open(local_tar_path, "r:bz2") as tar:
                for member in tar.getmembers():
                    if member.name in targets:
                        tar.extract(member, extract_dir)
            os.remove(local_tar_path)

            # Normalize filenames
            if os.path.exists(os.path.join(extract_dir, "zephyr.ro.elf")):
                os.rename(
                    os.path.join(extract_dir, "zephyr.ro.elf"),
                    os.path.join(extract_dir, "ec.RO.elf"),
                )
            z_rw_path = os.path.join(extract_dir, "zephyr.rw.elf")
            if os.path.exists(z_rw_path):
                os.rename(z_rw_path, os.path.join(extract_dir, "ec.RW.elf"))
            return extract_dir, gcs_path
        except Exception as e:
            logger.error("Extraction error for %s: %s", local_tar_path, e)
            return None, None

    def _blob_exists(self, gcs_path: str) -> bool:
        """Checks if a blob exists in GCS."""
        if not self.storage_client:
            return False
        bucket_name = self.GCS_BUCKET_BASE.removeprefix("gs://")
        try:
            bucket = self.storage_client.bucket(bucket_name)
            blob = bucket.blob(gcs_path)
            return blob.exists()
        except Exception:
            return False

    def _attempt_download(self, gcs_path: str, local_dest: str) -> bool:
        """Attempts to download a blob from GCS."""
        if not self.storage_client:
            return False
        bucket_name = self.GCS_BUCKET_BASE.removeprefix("gs://")
        try:
            bucket = self.storage_client.bucket(bucket_name)
            blob = bucket.blob(gcs_path)
            if not blob.exists():
                return False
            blob.download_to_filename(local_dest)
            return True
        except Exception as e:
            logger.error("Failed to download %s: %s", gcs_path, e)
            return False

    def _resolve_symbol(self, elf_path: str, address_str: str) -> str:
        """Resolves an address to a symbol using addr2line."""
        if not elf_path or not os.path.exists(elf_path):
            return "elf_missing"
        try:
            cmd = [self.ADDR2LINE_BIN, "-f", "-e", elf_path, address_str]
            output = subprocess.check_output(cmd, text=True).strip().split("\n")
            return output[0] if output else "unknown"
        except Exception as e:
            logger.debug("Failed to resolve symbol for %s: %s", address_str, e)
            return "lookup_failed"

    def extract_crash_reason(self, file_path: str) -> str:
        crash_reason = "unknown"
        try:
            with open(file_path, "r", encoding="utf-8") as file:
                file_content = file.read()
                logger.debug("Crash file content: %s", file_content)
                for pattern in self.panic_regex_patterns:
                    match = re.search(pattern, file_content)
                    logger.debug("Testing pattern %s", pattern)
                    if match:
                        logger.debug("Matched pattern %s", pattern)
                        match_text = match.group(1)[-4:].upper()
                        logger.debug("Matching text: %s", match_text)
                        if match_text in self.error_codes:
                            crash_reason = self.error_codes[match_text]
                            break
        except FileNotFoundError:
            logger.error("Error: File not found at %s", file_path)
        except UnicodeDecodeError:
            logger.error(
                "Error: Could not decode file %s."
                " Please check the file encoding.",
                file_path,
            )
        except IOError as e:
            logger.error("An I/O error occurred: %s", e)
        return crash_reason

    def extract_program_counter(self, file_path: str) -> str:
        program_counter = "unknown"
        try:
            with open(file_path, "r", encoding="utf-8") as file:
                file_content = file.read()
                logger.debug("Crash file content: %s", file_content)
                for pattern in self.pc_regex_patterns:
                    match = re.search(pattern, file_content)
                    logger.debug("Testing PC pattern %s", pattern)
                    if match:
                        logger.debug("Matched PC pattern %s", pattern)
                        match_text = match.group(1).lower()
                        program_counter = match_text
                        break
        except FileNotFoundError:
            logger.error("Error: File not found at %s", file_path)
        except UnicodeDecodeError:
            logger.error(
                "Error: Could not decode file %s."
                " Please check the file encoding.",
                file_path,
            )
        except IOError as e:
            logger.error("An I/O error occurred: %s", e)
        return program_counter

    def analyze(
        self, results: list[dict[str, Any]], full_results: dict[str, Any]
    ) -> None:
        if "crash_count" not in full_results:
            full_results["crash_count"] = 0
        if "has_ec_crashes" not in full_results:
            full_results["has_ec_crashes"] = False
        for result in results:
            if "crash_reason" not in result:
                result["crash_reason"] = []
            if "program_counter" not in result:
                result["program_counter"] = []
            if "crash_function_ro" not in result:
                result["crash_function_ro"] = []
            if "crash_function_rw" not in result:
                result["crash_function_rw"] = []
            if "artifact_link_ro" not in result:
                result["artifact_link_ro"] = []
            if "artifact_link_rw" not in result:
                result["artifact_link_rw"] = []
            result["crash_count"] = len(result["crash_reason"])
            if "has_ec_crash" in result:
                drop_indices = []
                for index, has_crash in enumerate(result["has_ec_crash"]):
                    if not isinstance(has_crash, bool):
                        try:
                            has_crash = bool(has_crash)
                        except (ValueError, TypeError):
                            logger.error(
                                "Could not convert %s to bool", has_crash
                            )
                            drop_indices.append(index)
                drop_indices.sort()
                for drop_index in reversed(drop_indices):
                    result["has_ec_crash"].pop(drop_index)

                full_results["crash_count"] -= len(result["crash_reason"])
                full_results["has_ec_crashes"] = any(
                    result["has_ec_crash"]
                ) or full_results.get("has_ec_crashes", False)
                if len(result["has_ec_crash"]) == 0:
                    result["has_ec_crash"] = [False]
                    continue
                tast_test_name = result["test"].removeprefix("tast.")
                board = result.get("board", "UNKNOWN")
                model = result.get("model", "UNKNOWN")
                ro_ver = result.get("ec_ro_version", "UNKNOWN")
                rw_ver = result.get("ec_rw_version", "UNKNOWN")
                ro_fwid = result.get("ec_ro_fwid", "UNKNOWN")
                rw_fwid = result.get("ec_rw_fwid", "UNKNOWN")
                for index, crash_status in enumerate(result["has_ec_crash"]):
                    if crash_status:
                        logger.debug("Found crash in %s", result)
                        log_url = result["log_urls"][index]
                        if result["statuses"][index] == "TEST_NA":
                            continue
                        logs = self.get_logs(
                            tast_test_name=tast_test_name,
                            log_url=log_url,
                            glob_patterns=[
                                "gs://{bucket}/{log}/cros-test*/"
                                "cros-test/results/tast/tests/"
                                "{tast_test_name}/**/*.eccrash",
                                "gs://{bucket}/{log}/cros-test*/"
                                "cros-test/results/tast/tests/"
                                "{tast_test_name}.[0-9]/**/*.eccrash",
                            ],
                            patterns=[r".*\.eccrash"],
                        )
                        for log in logs:
                            logger.debug("Found crash file %s", log)
                            reason = self.extract_crash_reason(log)
                            logger.debug("Crash Reason: %s", reason)
                            result["crash_reason"].append(reason)
                            pc = self.extract_program_counter(log)
                            logger.debug("Program Counter: %s", pc)
                            result["program_counter"].append(pc)

                            # Resolve symbols
                            ro_func = "N/A"
                            rw_func = "N/A"
                            ro_link = "N/A"
                            rw_link = "N/A"
                            if pc != "unknown":
                                # --- Resolve RO ---
                                if ro_ver and ro_ver != "UNKNOWN":
                                    ro_dir, ro_path = (
                                        self._download_and_extract(
                                            board, model, ro_ver, ro_fwid
                                        )
                                    )
                                    if ro_dir:
                                        ro_func = self._resolve_symbol(
                                            os.path.join(ro_dir, "ec.RO.elf"),
                                            pc,
                                        )
                                        ro_link = ro_path or "unknown"
                                    else:
                                        ro_func = "download_failed"
                                        ro_link = "download_failed"

                                # --- Resolve RW ---
                                if rw_ver and rw_ver != "UNKNOWN":
                                    if (
                                        rw_ver == ro_ver
                                        and rw_fwid == ro_fwid
                                        and ro_link
                                        not in ["download_failed", "N/A"]
                                    ):
                                        rw_dir, rw_path = ro_dir, ro_path
                                    else:
                                        rw_dir, rw_path = (
                                            self._download_and_extract(
                                                board, model, rw_ver, rw_fwid
                                            )
                                        )

                                    if rw_dir:
                                        rw_func = self._resolve_symbol(
                                            os.path.join(rw_dir, "ec.RW.elf"),
                                            pc,
                                        )
                                        rw_link = rw_path or "unknown"
                                    else:
                                        rw_func = "download_failed"
                                        rw_link = "download_failed"

                            result["crash_function_ro"].append(ro_func)
                            result["crash_function_rw"].append(rw_func)
                            result["artifact_link_ro"].append(ro_link)
                            result["artifact_link_rw"].append(rw_link)

                result["crash_count"] = len(result["crash_reason"])
                full_results["crash_count"] += len(result["crash_reason"])
            self.enforce_array_type(result["has_ec_crash"], bool, False)
