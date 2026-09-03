#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Host HDLC client for ChromeOS Zephyr EC.

Communicates with the EC over serial UART or TCP socket using Pigweed HDLC
framing, CRC32 validation, and multi-channel protocol routing (Host Commands
0x03, Text Logs 0x01, Tokenized Logs 0x02, RPC 0x04).
"""

import argparse
import binascii
import socket
import struct
import time
from typing import Any, Optional, Tuple
import zlib


try:
    import serial
except ImportError:
    serial = None

# HDLC Framing Constants
FLAG = 0x7E
ESCAPE = 0x7D
ESCAPE_XOR = 0x20

# Standard Channel Addresses (Low nibble standard)
ADDR_TEXT_LOG = 0x01
ADDR_TOKENIZED_LOG = 0x02
ADDR_HOST_CMD = 0x03
ADDR_RPC = 0x04
ADDR_CRASH_DUMP = 0x05
ADDR_CONSOLE_CMD = 0x06
ADDR_CUSTOM_STOP = 0x10

# Host Command Constants
EC_CMD_HELLO = 0x0001
EC_CMD_GET_VERSION = 0x0002
EC_CMD_FLASH_INFO = 0x0010
EC_HOST_REQUEST_VERSION = 3
EC_HOST_RESPONSE_VERSION = 3
EC_RES_SUCCESS = 0


def encode_address(address: int) -> bytes:
    """Encodes an HDLC address as a one-terminated LSB varint per Pigweed HDLC."""
    result = bytearray()
    while True:
        result += bytes([(address & 0x7F) << 1])
        address >>= 7
        if address == 0:
            break
    result[-1] |= 0x1
    return bytes(result)


def decode_address(frame: bytes) -> Tuple[int, int]:
    """Decodes a one-terminated LSB HDLC address varint from raw frame data."""
    result = 0
    length = 0
    while length < len(frame):
        byte = frame[length]
        result |= (byte >> 1) << (length * 7)
        length += 1
        if byte & 0x1 == 0x1:
            break
    return result, length


def encode_hdlc_frame(address: int, payload: bytes) -> bytes:
    """Encodes an unnumbered information (UI) HDLC frame with CRC-32."""
    frame = encode_address(address) + b"\x03" + payload
    frame += (zlib.crc32(frame) & 0xFFFFFFFF).to_bytes(4, "little")
    frame = frame.replace(bytes([ESCAPE]), b"\x7d\x5d")
    frame = frame.replace(bytes([FLAG]), b"\x7d\x5e")
    return bytes([FLAG]) + frame + bytes([FLAG])


class HdlcDecoder:
    """Incremental streaming HDLC frame decoder with raw console text stream support."""

    def __init__(self, on_unframed_text: Optional[Any] = None):
        """Initializes decoder state."""
        self._decoded_data = bytearray()
        self._raw_data = bytearray()
        self._state = 0  # 0: INTERFRAME, 1: FRAME, 2: FRAME_ESCAPE
        self.raw_text_buffer = bytearray()
        self.on_unframed_text = on_unframed_text

    def _flush_raw_text(self):
        """Flushes any accumulated raw unescaped text to callback."""
        if self.raw_text_buffer and self.on_unframed_text:
            text = self.raw_text_buffer.decode("utf-8", errors="replace")
            self.on_unframed_text(text)
        self.raw_text_buffer.clear()

    def process_byte(self, byte: int) -> Optional[dict]:
        """Processes a single incoming byte and returns a decoded frame if complete."""
        frame_dict = None
        self._raw_data.append(byte)

        if self._state == 0:  # INTERFRAME
            if byte == FLAG:
                if len(self.raw_text_buffer) > 0:
                    self._flush_raw_text()
                self._state = 1
                self._raw_data = bytearray([FLAG])
                self._decoded_data.clear()
            else:
                if 0x20 <= byte <= 0x7E or byte in (
                    ord("\n"),
                    ord("\r"),
                    ord("\t"),
                ):
                    self.raw_text_buffer.append(byte)
                    if byte == ord("\n") or len(self.raw_text_buffer) >= 256:
                        self._flush_raw_text()

        elif self._state == 1:  # FRAME
            if byte == FLAG:
                if len(self._decoded_data) >= 6:
                    frame_dict = self._parse_frame(bytes(self._decoded_data))
                self._state = 1
                self._raw_data = bytearray([FLAG])
                self._decoded_data.clear()
            elif byte == ESCAPE:
                self._state = 2
            else:
                self._decoded_data.append(byte)

        elif self._state == 2:  # FRAME_ESCAPE
            if byte == FLAG:
                self._state = 1
                self._raw_data = bytearray([FLAG])
                self._decoded_data.clear()
            elif byte in (0x5D, 0x5E):
                self._state = 1
                self._decoded_data.append(byte ^ ESCAPE_XOR)
            else:
                self._state = 0
                self._raw_data.clear()
                self._decoded_data.clear()

        return frame_dict

    def _parse_frame(self, data: bytes) -> Optional[dict]:
        """Validates CRC-32 and extracts HDLC address, control, and payload."""
        if len(data) < 6:
            return None

        payload_with_hdr = data[:-4]
        expected_crc = struct.unpack("<I", data[-4:])[0]
        actual_crc = zlib.crc32(payload_with_hdr) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            return None

        addr, addr_len = decode_address(payload_with_hdr)
        if addr < 0 or addr_len >= len(payload_with_hdr):
            return None

        ctrl = payload_with_hdr[addr_len]
        payload = payload_with_hdr[addr_len + 1 :]
        return {"address": addr, "control": ctrl, "data": payload}


def build_host_command_packet(
    command: int, command_version: int = 0, params: bytes = b""
) -> bytes:
    """Builds a ChromeOS EC Host Command Protocol v3 request packet."""
    pkt_no_csum = (
        struct.pack(
            "<BBHBBH",
            EC_HOST_REQUEST_VERSION,
            0,
            command,
            command_version,
            0,
            len(params),
        )
        + params
    )
    csum = (-sum(pkt_no_csum)) & 0xFF
    return (
        struct.pack(
            "<BBHBBH",
            EC_HOST_REQUEST_VERSION,
            csum,
            command,
            command_version,
            0,
            len(params),
        )
        + params
    )


class Transport:
    """Abstract connection transport (Serial or Socket)."""

    def write(self, data: bytes):
        """Writes raw bytes to the transport."""
        raise NotImplementedError

    def read(self, size: int = 4096) -> bytes:
        """Reads raw bytes from the transport."""
        raise NotImplementedError

    def close(self):
        """Closes the transport connection."""
        raise NotImplementedError


class SerialTransport(Transport):
    """Serial UART transport via pyserial."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.1):
        """Initializes serial connection on specified port."""
        if serial is None:
            raise ImportError(
                "pyserial package is required for serial communication"
            )
        self.ser = serial.Serial(port, baud, timeout=timeout)

    def write(self, data: bytes):
        """Writes data to serial port and flushes buffer."""
        self.ser.write(data)
        self.ser.flush()

    def read(self, size: int = 4096) -> bytes:
        """Reads data from serial port."""
        return self.ser.read(size)

    def close(self):
        """Closes serial port."""
        self.ser.close()


class SocketTransport(Transport):
    """TCP socket transport."""

    def __init__(self, host: str, port: int, timeout: float = 0.5):
        """Initializes TCP socket connection to target host and port."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.settimeout(timeout)

    def write(self, data: bytes):
        """Writes data to TCP socket."""
        self.sock.sendall(data)

    def read(self, size: int = 4096) -> bytes:
        """Reads data from TCP socket."""
        try:
            return self.sock.recv(size)
        except socket.timeout:
            return b""

    def close(self):
        """Closes TCP socket."""
        self.sock.close()


class HdlcClient:
    """Client for HDLC communication with ChromeOS EC."""

    def __init__(self, transport: Transport):
        """Initializes HDLC client using specified transport."""
        self.transport = transport
        self.decoder = HdlcDecoder()

    def send_frame(self, address: int, payload: bytes):
        """Encodes and sends an HDLC UI frame to target address."""
        frame = encode_hdlc_frame(address, payload)
        self.transport.write(frame)

    def recv_frame(
        self, expected_address: Optional[int] = None, timeout_sec: float = 2.0
    ) -> Optional[dict]:
        """Receives and decodes an HDLC frame within timeout window."""
        start = time.time()
        while time.time() - start < timeout_sec:
            raw = self.transport.read(512)
            if raw:
                for b in raw:
                    parsed = self.decoder.process_byte(b)
                    if parsed:
                        if (
                            expected_address is None
                            or parsed["address"] == expected_address
                        ):
                            return parsed
            time.sleep(0.005)
        return None

    def enter_hdlc_server(self) -> bool:
        """Sends command to enter live HDLC bypass mode on EC shell."""
        self.exit_hdlc_server()
        self.transport.write(b"\r\n")
        time.sleep(0.1)
        self.transport.read()
        self.transport.write(b"hdlc_server\r\n")
        banner = ""
        start = time.time()
        while time.time() - start < 3.0:
            chunk = self.transport.read().decode(errors="ignore")
            if chunk:
                banner += chunk
                if "Entering HDLC mode" in banner:
                    return True
            time.sleep(0.05)
        return "Entering HDLC mode" in banner or "hdlc" in banner.lower()

    def exit_hdlc_server(self):
        """Sends STOP frame on channel 0x10 to exit HDLC mode."""
        self.send_frame(ADDR_CUSTOM_STOP, b"STOP")
        time.sleep(0.2)
        self.transport.read()

    def host_command(
        self,
        command: int,
        command_version: int = 0,
        params: bytes = b"",
        timeout_sec: float = 2.0,
    ) -> Tuple[int, bytes]:
        """Executes Host Command Protocol v3 request over channel 0x03."""
        req_pkt = build_host_command_packet(command, command_version, params)
        self.send_frame(ADDR_HOST_CMD, req_pkt)
        resp_frame = self.recv_frame(
            expected_address=ADDR_HOST_CMD, timeout_sec=timeout_sec
        )
        if not resp_frame:
            raise TimeoutError("No HDLC response received for host command")

        data = resp_frame["data"]
        if len(data) < 8:
            raise ValueError(
                f"Malformed host command response: len={len(data)}"
            )

        _, _, r_res, r_len, _ = struct.unpack("<BBHHH", data[:8])
        resp_data = data[8 : 8 + r_len]
        return r_res, resp_data

    def cmd_hello(self, input_val: int = 0x12345678) -> int:
        """Sends EC_CMD_HELLO and returns response integer value."""
        params = struct.pack("<I", input_val)
        res, data = self.host_command(EC_CMD_HELLO, params=params)
        if res != EC_RES_SUCCESS:
            raise RuntimeError(f"EC_CMD_HELLO failed with error {res}")
        return struct.unpack("<I", data)[0]

    def cmd_get_version(self) -> dict:
        """Sends EC_CMD_GET_VERSION and returns parsed version dict."""
        res, data = self.host_command(EC_CMD_GET_VERSION)
        if res != EC_RES_SUCCESS:
            raise RuntimeError(f"EC_CMD_GET_VERSION failed with error {res}")

        ro_str = (
            data[0:32].split(b"\0")[0].decode("ascii", errors="replace").strip()
        )
        rw_str = (
            data[32:64]
            .split(b"\0")[0]
            .decode("ascii", errors="replace")
            .strip()
        )
        cur_img = (
            struct.unpack("<I", data[96:100])[0] if len(data) >= 100 else 0
        )
        img_name = (
            "RO"
            if cur_img == 0
            else ("RW" if cur_img == 1 else f"Unknown({cur_img})")
        )
        return {"ro": ro_str, "rw": rw_str, "image": img_name}


def main():
    """Main CLI entry point for HDLC client."""
    parser = argparse.ArgumentParser(
        description="Pigweed HDLC Client for ChromeOS EC"
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--port", "-p", help="Serial PTY or TTY path")
    group.add_argument(
        "--socket", "-s", help="TCP socket host:port (e.g. 127.0.0.1:2718)"
    )

    parser.add_argument(
        "--baud", "-b", type=int, default=115200, help="UART baud rate"
    )
    parser.add_argument(
        "--enter",
        action="store_true",
        help="Send hdlc_server command before interacting",
    )
    parser.add_argument(
        "--exit",
        action="store_true",
        help="Send STOP frame before disconnecting",
    )

    subparsers = parser.add_subparsers(dest="command", help="Command to run")

    subparsers.add_parser("hello", help="Run EC_CMD_HELLO test")
    subparsers.add_parser("version", help="Read EC firmware version")
    subparsers.add_parser("test", help="Run full self-test verification suite")

    log_parser = subparsers.add_parser(
        "send-text", help="Send text log frame (Addr 0x01)"
    )
    log_parser.add_argument("text", help="Text to send")

    tok_parser = subparsers.add_parser(
        "send-token", help="Send tokenized log frame (Addr 0x02)"
    )
    tok_parser.add_argument(
        "hexdata", help="Hex string of binary token payload"
    )

    args = parser.parse_args()

    if args.port:
        transport = SerialTransport(args.port, baud=args.baud)
    else:
        host, port_str = args.socket.split(":")
        transport = SocketTransport(host, int(port_str))

    client = HdlcClient(transport)

    try:
        if args.enter:
            print("Entering HDLC mode on EC...")
            if not client.enter_hdlc_server():
                print("Warning: EC did not explicitly confirm HDLC entry")

        if args.command == "hello":
            in_val = 0x12345678
            out_val = client.cmd_hello(in_val)
            print(f"HELLO response: 0x{out_val:08x} (input 0x{in_val:08x})")

        elif args.command == "version":
            v = client.cmd_get_version()
            print(f"Current Image: {v['image']}")
            print(f"RO Version:    {v['ro']}")
            print(f"RW Version:    {v['rw']}")

        elif args.command == "send-text":
            msg = args.text.encode("utf-8")
            client.send_frame(ADDR_TEXT_LOG, msg)
            resp = client.recv_frame(
                expected_address=ADDR_TEXT_LOG, timeout_sec=2.0
            )
            if resp:
                print(
                    "Received response:",
                    resp["data"].decode("utf-8", errors="replace"),
                )
            else:
                print("No response received.")

        elif args.command == "send-token":
            tok = bytes.fromhex(args.hexdata)
            client.send_frame(ADDR_TOKENIZED_LOG, tok)
            resp = client.recv_frame(
                expected_address=ADDR_TOKENIZED_LOG, timeout_sec=2.0
            )
            if resp:
                print(
                    "Received response:",
                    binascii.hexlify(resp["data"]).decode(),
                )
            else:
                print("No response received.")

        elif args.command == "test":
            print("Running full HDLC validation suite...")
            # 1. Hello
            out_val = client.cmd_hello(0x12345678)
            assert out_val == 0x12345678 + 0x01020304
            print(f"  [PASS] EC_CMD_HELLO -> 0x{out_val:08x}")

            # 2. Version
            v = client.cmd_get_version()
            assert len(v["ro"]) > 0
            print(f"  [PASS] EC_CMD_GET_VERSION -> RO:{v['ro']} RW:{v['rw']}")

            # 3. Text log
            client.send_frame(ADDR_TEXT_LOG, b"Ping Text")
            resp = client.recv_frame(
                expected_address=ADDR_TEXT_LOG, timeout_sec=1.5
            )
            assert resp is not None
            print(
                f"  [PASS] Text Log Echo -> {resp['data'].decode(errors='replace')}"
            )

            # 4. Tokenized log
            client.send_frame(ADDR_TOKENIZED_LOG, b"\xde\xad\xbe\xef")
            resp = client.recv_frame(
                expected_address=ADDR_TOKENIZED_LOG, timeout_sec=1.5
            )
            assert resp is not None and resp["data"] == b"\xde\xad\xbe\xef"
            print(
                f"  [PASS] Tokenized Log Echo -> {binascii.hexlify(resp['data']).decode()}"
            )

            print("ALL TESTS PASSED!")

        if args.exit:
            print("Exiting HDLC mode on EC...")
            client.exit_hdlc_server()

    finally:
        transport.close()


if __name__ == "__main__":
    main()
