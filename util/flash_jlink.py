#!/usr/bin/env python3
# Copyright 2020 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=logging-fstring-interpolation

"""Flashes firmware using Segger J-Link or GDB.

This script requires Segger hardware attached via JTAG/SWD.

When flashing with GDB, start the server with:

JLinkGDBServerCLExe -port 19020 -device STM32H743ZI -if SWD -nogui

where device is one of:

* STM32H743ZI (icetower)
* STM32F412CG (dragonclaw)
* NPCX998F (quincy)

See
https://chromium.googlesource.com/chromiumos/platform/ec/+/HEAD/docs/fingerprint/fingerprint-debugging.md#flash
for instructions.
"""

import argparse
import logging
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


DEFAULT_SEGGER_REMOTE_PORT = 19020

# Commands are documented here: https://wiki.segger.com/J-Link_Commander
JLINK_COMMANDS = """
exitonerror 1
r
loadfile {FIRMWARE} {FLASH_ADDRESS}
r
go
exit
"""

# Commands for GDB flashing.
GDB_COMMANDS = """
target extended-remote {HOST}:{PORT}
monitor device {DEVICE}
monitor speed {SPEED}
monitor reset
restore {FIRMWARE} binary {FLASH_ADDRESS}
monitor reset
monitor go
disconnect
quit
"""


class BoardConfig:
    """Board configuration."""

    def __init__(self, interface, device, speed, flash_address):
        self.interface = interface
        self.device = device
        self.speed = speed
        self.flash_address = flash_address


SWD_INTERFACE = "SWD"
INTERFACE_SPEED_AUTO = "auto"
INTERFACE_SPEED_4_MHZ = "4000"
STM32_DEFAULT_FLASH_ADDRESS = "0x8000000"
NPCX_DEFAULT_FLASH_ADDRESS = "0x64000000"
VERIFY_FLASH_DEFAULT = True
DRAGONCLAW_CONFIG = BoardConfig(
    interface=SWD_INTERFACE,
    device="STM32F412CG",
    speed=INTERFACE_SPEED_AUTO,
    flash_address=STM32_DEFAULT_FLASH_ADDRESS,
)
ICETOWER_CONFIG = BoardConfig(
    interface=SWD_INTERFACE,
    device="STM32H743ZI",
    speed=INTERFACE_SPEED_AUTO,
    flash_address=STM32_DEFAULT_FLASH_ADDRESS,
)
HELIPILOT_CONFIG = BoardConfig(
    interface=SWD_INTERFACE,
    device="NPCX998F",
    speed=INTERFACE_SPEED_4_MHZ,
    flash_address=NPCX_DEFAULT_FLASH_ADDRESS,
)

BOARD_CONFIGS = {
    "dragonclaw": DRAGONCLAW_CONFIG,
    "bloonchipper": DRAGONCLAW_CONFIG,
    "nucleo-f412zg": DRAGONCLAW_CONFIG,
    "dartmonkey": ICETOWER_CONFIG,
    "icetower": ICETOWER_CONFIG,
    "nucleo-dartmonkey": ICETOWER_CONFIG,
    "nucleo-h743zi": ICETOWER_CONFIG,
    "helipilot": HELIPILOT_CONFIG,
}


def is_tcp_port_open(host: str, tcp_port: int) -> bool:
    """Checks if the TCP host port is open."""

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2)  # 2 Second Timeout
    try:
        sock.connect((host, tcp_port))
        sock.shutdown(socket.SHUT_RDWR)
    except ConnectionRefusedError:
        return False
    except socket.timeout:
        return False
    finally:
        sock.close()
    # Other errors are propagated as odd exceptions.

    # We shutdown and closed the connection, but the server may need a second
    # to start listening again. If the following error is seen, this timeout
    # should be increased. 300ms seems to be the minimum.
    #
    # Connecting to J-Link via IP...FAILED: Can not connect to J-Link via \
    #   TCP/IP (127.0.0.1, port 19020)
    time.sleep(0.5)
    return True


def flash_remote_gdb(
    gdb_exe, host, port, device, speed, flash_address, firmware_file
):
    """Uses a local GDB client connected to remote GDB server to flash the device."""
    logging.debug(f"Checking connection to {host}:{port}.")
    if not is_tcp_port_open(host, port):
        logging.error(f"GDB server not listening on {host}:{port}.")
        logging.error(
            "Ensure that JLinkGDBServerCLExe is running on the remote host."
        )
        return 1

    tmp_cmd = tempfile.NamedTemporaryFile(mode="w", delete=False)
    tmp_cmd.write(
        GDB_COMMANDS.format(
            HOST=host,
            PORT=port,
            DEVICE=device,
            SPEED=speed,
            FLASH_ADDRESS=flash_address,
            FIRMWARE=firmware_file,
        )
    )
    tmp_cmd.close()

    cmd = [
        gdb_exe,
        "-nx",  # Do not execute .gdbinit
        "--batch",  # Exit after processing the script
        "-x",
        tmp_cmd.name,
    ]

    logging.debug('Running GDB command: "%s"', " ".join(cmd))
    completed_process = subprocess.run(cmd, check=False)

    os.remove(tmp_cmd.name)
    logging.debug("GDB return code: %d", completed_process.returncode)
    return completed_process.returncode


def flash_local_jlink(
    jlink_exe, device, interface, speed, flash_address, firmware_file
):
    """Uses jlink to flash device locally over USB."""
    tmp_cmd = tempfile.NamedTemporaryFile(mode="wb", delete=False)
    tmp_cmd.write(
        JLINK_COMMANDS.format(
            FIRMWARE=firmware_file,
            FLASH_ADDRESS=flash_address,
        ).encode("utf-8")
    )
    tmp_cmd.close()

    cmd = [
        jlink_exe,
        "-device",
        device,
        "-if",
        interface,
        "-speed",
        speed,
        "-autoconnect",
        "1",
        "-NoGui",
        "1",
        "-CommandFile",
        tmp_cmd.name,
    ]

    logging.debug('Running JLink command: "%s"', " ".join(cmd))
    completed_process = subprocess.run(cmd, check=False)

    os.remove(tmp_cmd.name)
    logging.debug("JLink return code: %d", completed_process.returncode)
    return completed_process.returncode


def main(argv: list):
    """Main function."""
    parser = argparse.ArgumentParser()

    default_jlink = "./JLink_Linux_V810a_x86_64/JLinkExe"
    if shutil.which(default_jlink) is None:
        default_jlink = "JLinkExe"
    parser.add_argument(
        "--jlink",
        "-j",
        help="JLinkExe path for local USB flashing (default: "
        + default_jlink
        + ")",
        default=default_jlink,
    )

    parser.add_argument(
        "--gdb",
        "-g",
        help="GDB executable path for flashing (default: arm-none-eabi-gdb)",
        default="arm-none-eabi-gdb",
    )

    parser.add_argument(
        "--remote",
        "-n",
        help="Use TCP/IP host[:port] to connect to JLinkGDBServerCLExe. "
        "If unspecified, connect locally via USB using JLinkExe.",
    )

    default_board = "bloonchipper"
    parser.add_argument(
        "--board",
        "-b",
        help="Board (default: " + default_board + ")",
        default=default_board,
    )

    default_firmware = os.path.join("./build", default_board, "ec.bin")
    parser.add_argument(
        "--image",
        "-i",
        help="Firmware binary (default: " + default_firmware + ")",
        default=default_firmware,
    )

    log_level_choices = ["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"]
    parser.add_argument(
        "--log_level", "-l", choices=log_level_choices, default="DEBUG"
    )

    args = parser.parse_args(argv)
    logging.basicConfig(level=args.log_level)

    if args.board not in BOARD_CONFIGS:
        logging.error('Unable to find a config for board: "%s"', args.board)
        sys.exit(1)

    config = BOARD_CONFIGS[args.board]

    args.image = os.path.realpath(args.image)

    if args.remote:
        logging.debug(f"Connecting to remote GDB over TCP/IP {args.remote}.")
        remote_components = args.remote.split(":")
        host = remote_components[0]
        port = DEFAULT_SEGGER_REMOTE_PORT

        try:
            ip_addr = socket.gethostbyname(host)
        except socket.gaierror as err:
            logging.error(f'Failed to resolve host "{host}": {err}.')
            sys.exit(1)

        if len(remote_components) == 2:
            try:
                port = int(remote_components[1])
            except ValueError:
                logging.error(
                    f'Given remote port "{remote_components[1]}" is malformed.'
                )
                sys.exit(1)

        ret_code = flash_remote_gdb(
            gdb_exe=args.gdb,
            host=ip_addr,
            port=port,
            device=config.device,
            speed=config.speed,
            flash_address=config.flash_address,
            firmware_file=args.image,
        )
    else:
        ret_code = flash_local_jlink(
            jlink_exe=args.jlink,
            device=config.device,
            interface=config.interface,
            speed=config.speed,
            flash_address=config.flash_address,
            firmware_file=args.image,
        )

    return ret_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
