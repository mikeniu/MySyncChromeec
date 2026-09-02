#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Pigweed Console integration for ChromeOS EC Host Commands.

This tool establishes a pw_rpc / HDLC connection to a ChromeOS EC device
over UART serial or socket and opens an interactive Pigweed Console REPL
for sending and debugging host commands.
"""

# pylint: disable=import-error,import-outside-toplevel,broad-exception-caught,unused-import

import argparse
import logging
import struct
import sys
from typing import Dict


try:
    from pw_console import embed

    HAS_PW_CONSOLE = True
except ImportError:
    HAS_PW_CONSOLE = False

try:
    from pw_hdlc.rpc import default_channels
    from pw_hdlc.rpc import HdlcRpcClient
    from pw_hdlc.rpc import SerialReader
    import serial
except ImportError:
    print(
        "Error: Missing required Python dependencies (pw_hdlc / pyserial).\n"
        "Please ensure pw_hdlc and pyserial are available in your environment.",
        file=sys.stderr,
    )
    sys.exit(1)

# Standard ChromeOS EC Status Codes
EC_RES_SUCCESS = 0
EC_RES_INVALID_COMMAND = 1
EC_RES_ERROR = 2
EC_RES_INVALID_PARAM = 3
EC_RES_ACCESS_DENIED = 4
EC_RES_INVALID_RESPONSE = 5
EC_RES_INVALID_VERSION = 6
EC_RES_INVALID_CHECKSUM = 7
EC_RES_IN_PROGRESS = 8
EC_RES_UNAVAILABLE = 9
EC_RES_TIMEOUT = 10
EC_RES_OVERFLOW = 11
EC_RES_INVALID_HEADER = 12
EC_RES_REQUEST_TRUNCATED = 13
EC_RES_RESPONSE_TOO_BIG = 14
EC_RES_BUS_ERROR = 15
EC_RES_BUSY = 16

# Standard ChromeOS EC Host Commands
EC_CMD_HELLO = 0x0001
EC_CMD_GET_VERSION = 0x0002
EC_CMD_READ_MEMMAP = 0x0007
EC_CMD_GET_CHIP_INFO = 0x0009
EC_CMD_GET_BOARD_VERSION = 0x000C
EC_CMD_REBOOT_EC = 0x00D2


class ChromeOsEcClient:
    """Helper client for sending Host Commands over Pigweed RPC."""

    def __init__(self, rpc_client):
        self._rpc = rpc_client
        self._service = self._rpc.rpcs().chromiumos.ec.HostCommandService

    def send_host_command(
        self,
        cmd: int,
        ver: int = 0,
        payload: bytes = b"",
        timeout_s: float = 3.0,
    ) -> bytes:
        """Send a raw host command and return response payload bytes."""
        status, response = self._service.ExecuteCommand(
            command=cmd,
            version=ver,
            request_payload=payload,
            pw_rpc_timeout_s=timeout_s,
        )
        if not status.ok():
            raise RuntimeError(
                f"RPC communication failed with status: {status}"
            )

        if response.ec_status != EC_RES_SUCCESS:
            raise RuntimeError(
                f"EC Host Command 0x{cmd:04x} returned error status: "
                f"{response.ec_status} (0x{response.ec_status:x})"
            )

        return response.response_payload

    def hello(self, input_val: int = 0xA0B0C0D0) -> int:
        """Execute EC_CMD_HELLO (0x04) and verify handshake."""
        req_data = struct.pack("<I", input_val)
        resp_data = self.send_host_command(EC_CMD_HELLO, 0, req_data)
        (out_val,) = struct.unpack("<I", resp_data)
        expected = (input_val + 0x01020304) & 0xFFFFFFFF
        is_match = out_val == expected
        print(
            f"Hello handshake: sent=0x{input_val:08x}, "
            f"received=0x{out_val:08x} (Match: {is_match})"
        )
        return out_val

    def get_version(self) -> Dict[str, str]:
        """Execute EC_CMD_GET_VERSION (0x02) and decode firmware version info."""
        resp = self.send_host_command(EC_CMD_GET_VERSION, 0)
        # struct ec_response_get_version:
        #   char version_string_ro[32];
        #   char version_string_rw[32];
        #   char reserved[32];
        #   uint32_t current_image;
        if len(resp) < 100:
            raise ValueError(
                f"Response size {len(resp)} too short for ec_response_get_version"
            )

        ro_raw, rw_raw, _, current_img = struct.unpack(
            "<32s32s32sI", resp[:100]
        )
        ro_str = ro_raw.split(b"\x00")[0].decode("utf-8", errors="replace")
        rw_str = rw_raw.split(b"\x00")[0].decode("utf-8", errors="replace")
        image_name = {0: "Unknown", 1: "RO", 2: "RW"}.get(
            current_img, f"Image({current_img})"
        )

        print(f"Current Image: {image_name}")
        print(f"RO Version:    {ro_str}")
        print(f"RW Version:    {rw_str}")
        return {
            "current_image": image_name,
            "version_ro": ro_str,
            "version_rw": rw_str,
        }


def _get_host_command_proto_module():
    """Retrieve or dynamically construct the host_command_pb2 module."""
    try:
        from pw_host_command.proto import host_command_pb2

        return host_command_pb2
    except ImportError:
        pass

    try:
        import host_command_pb2

        return host_command_pb2
    except ImportError:
        pass

    # Build dynamically using google.protobuf
    import types

    from google.protobuf import descriptor_pool
    from google.protobuf.internal import builder

    pb2 = types.ModuleType("host_command_pb2")
    serialized_desc = (
        b"\n\x12host_command.proto\x12\rchromiumos.ec"
        b'"I\n\x12HostCommandRequest\x12\r\n\x07command\x18\x01(\r'
        b"\x12\r\n\x07version\x18\x02(\r\x12\x15\n\x0frequest_payload\x18\x03(\x0c"
        b'">\n\x13HostCommandResponse\x12\x0f\n\tec_status\x18\x01(\r'
        b"\x12\x16\n\x10response_payload\x18\x02(\x0c"
        b"2k\n\x12HostCommandService\x12U\n\x0eExecuteCommand\x12"
        b" chromiumos.ec.HostCommandRequest\x1a!chromiumos.ec.HostCommandResponse"
        b"b\x06proto3"
    )
    pool = descriptor_pool.Default()
    try:
        desc = pool.FindFileByName("host_command.proto")
    except KeyError:
        desc = pool.AddSerializedFile(serialized_desc)

    pb2.DESCRIPTOR = desc
    builder.BuildMessageAndEnumDescriptors(desc, pb2.__dict__)
    builder.BuildTopDescriptorsAndMessages(
        desc, "host_command_pb2", pb2.__dict__
    )
    return pb2


def main():
    """Main entrypoint for Pigweed EC console."""
    parser = argparse.ArgumentParser(
        description="Launch Pigweed Console with ChromeOS EC Host Command RPC support"
    )
    parser.add_argument(
        "-d",
        "--device",
        default="/dev/ttyUSB0",
        help="Serial port / PTY device (default: /dev/ttyUSB0)",
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="Serial baudrate (default: 115200)",
    )
    parser.add_argument(
        "--socket-addr",
        help="Connect via TCP socket instead of serial (e.g. localhost:3333)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose debug logging",
    )
    args = parser.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)

    host_command_pb2 = _get_host_command_proto_module()

    class SafeSerialReader(SerialReader):
        """Robust serial reader that gracefully handles transient PTY disconnects."""

        def read(self) -> bytes:
            """Read bytes safely, retrying on transient disconnects."""
            import time

            while True:
                try:
                    data = self._base_obj.read(
                        *self._read_args, **self._read_kwargs
                    )
                    if data:
                        return data
                    time.sleep(0.005)
                except (serial.SerialException, OSError):
                    time.sleep(0.05)
                    return b""
                except Exception:
                    return b""

    def _log_output_handler(data: bytes):
        try:
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(f"[EC LOG] {text}")
            if not text.endswith("\n"):
                sys.stdout.write("\n")
            sys.stdout.flush()
        except Exception:
            pass

    if args.socket_addr:
        import socket

        from pw_stream.stream_readers import SocketReader

        host, port_str = args.socket_addr.split(":")
        sock = socket.create_connection((host, int(port_str)))
        reader = SocketReader(sock, 4096)
        write_fn = sock.sendall
    else:
        ser = serial.Serial(args.device, args.baudrate, timeout=0.1)
        reader = SafeSerialReader(ser, 4096)
        write_fn = ser.write

    client = HdlcRpcClient(
        reader,
        [host_command_pb2],
        default_channels(write_fn),
        output=_log_output_handler,
    )

    ec_helper = ChromeOsEcClient(client)

    variables = {
        "client": client,
        "ec": ec_helper,
        "EC_CMD_HELLO": EC_CMD_HELLO,
        "EC_CMD_GET_VERSION": EC_CMD_GET_VERSION,
    }

    welcome_msg = (
        "====================================================\n"
        "  ChromeOS EC Host Command Pigweed Console (PoC)   \n"
        "====================================================\n"
        "Available helpers:\n"
        "  >>> ec.hello()\n"
        "  >>> ec.get_version()\n"
        "  >>> ec.send_host_command(cmd_id, version, payload)\n"
        "\n"
        "Raw RPC interface:\n"
        "  >>> client.rpcs().chromiumos.ec.HostCommandService.ExecuteCommand(...)\n"
    )

    if HAS_PW_CONSOLE:
        embed(
            global_vars=variables,
            local_vars={},
            welcome_message=welcome_msg,
        )
    else:
        import code
        import readline  # Enable arrow keys & command history

        print(welcome_msg)
        print(
            "Note: pw_console (prompt_toolkit) not found; running in standard interactive REPL."
        )
        code.interact(banner="", local=variables)


if __name__ == "__main__":
    main()
