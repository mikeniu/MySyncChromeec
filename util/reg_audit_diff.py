#!/usr/bin/env python3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Parse, capture, and compare EC register dumps across cold boot vs sysjump.

Usage examples:
  # 1. Compare two saved console captures (cold boot RO vs sysjump RO):
  ./util/reg_audit_diff.py --cold cold_ro.txt --sysjump sysjump_ro.txt

  # 2. Run automated Cold-Boot-RO vs RW->sysjump-RO audit over TCP EC UART:
  ./util/reg_audit_diff.py --uart 127.0.0.1:2403 --out-dir /tmp/reg_audit
"""

import argparse
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
import re
import select
import socket
import sys
import time
from typing import Dict, List, Optional, Tuple


@dataclass
class RegEntry:
    """Single scalar register snapshot entry."""

    stage: str
    module: str
    name: str
    offset: int
    size: int
    val_str: str
    val: Optional[int]
    flags: str

    @property
    def key(self) -> Tuple[str, str]:
        """Return (module, register_name) lookup key."""
        return (self.module, self.name)

    @property
    def is_volatile(self) -> bool:
        """Return True if register is marked volatile."""
        return "V" in self.flags

    @property
    def is_skipped(self) -> bool:
        """Return True if register read was skipped or faulted."""
        return self.val is None


@dataclass
class ModEntry:
    """Instantiated peripheral module metadata and clock gate state."""

    stage: str
    name: str
    type_name: str
    base: int
    clocked: int


@dataclass
class RegDump:
    """Parsed register snapshot for a single boot stage."""

    stage: str = ""
    image: str = ""
    jumped: int = 0
    reset_flags: int = 0
    modules: Dict[str, ModEntry] = field(default_factory=dict)
    regs: Dict[Tuple[str, str], RegEntry] = field(default_factory=dict)
    core_regs: Dict[str, int] = field(default_factory=dict)


RE_BEGIN = re.compile(
    r"REGDUMP_BEGIN\|stage=([^|]+)\|img=([^|]+)"
    r"\|jumped=(\d+)\|reset=(0x[0-9a-fA-F]+)"
)
RE_MOD = re.compile(
    r"MOD\|([^|]+)\|([^|]+)\|([^|]+)\|(0x[0-9a-fA-F]+)\|clk=(\d+)"
)
RE_REG = re.compile(
    r"REG\|([^|]+)\|([^|]+)\|([^|]+)\|(0x[0-9a-fA-F]+)"
    r"\|(\d+)\|([^|]+)\|([^\r\n]+)"
)
RE_CORE = re.compile(r"CORE\|([^|]+)\|([^\r\n]+)")


class EcUartClient:
    """Minimal TCP socket client for driving EC console commands."""

    def __init__(self, host: str, port: int):
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.sock.setblocking(False)

    def close(self) -> None:
        """Close the underlying TCP connection."""
        self.sock.close()

    def drain(self, duration: float = 0.5) -> str:
        """Read and discard any pending UART output for duration seconds."""
        end = time.time() + duration
        chunks: List[str] = []
        while time.time() < end:
            r, _, _ = select.select([self.sock], [], [], 0.05)
            if r:
                data = self.sock.recv(65536)
                if data:
                    chunks.append(data.decode("utf-8", errors="replace"))
        return "".join(chunks)

    def send_cmd(
        self,
        cmd: str,
        end_marker: Optional[str] = None,
        timeout: float = 30.0,
        idle_timeout: float = 1.5,
    ) -> str:
        """Send an EC console command and collect output until marker/idle."""
        self.drain(0.2)
        self.sock.sendall((cmd + "\r\n").encode("utf-8"))
        chunks: List[str] = []
        start = time.time()
        last_rx = start
        full_text = ""

        while time.time() - start < timeout:
            r, _, _ = select.select([self.sock], [], [], 0.1)
            if r:
                data = self.sock.recv(65536)
                if data:
                    s = data.decode("utf-8", errors="replace")
                    chunks.append(s)
                    full_text = "".join(chunks)
                    last_rx = time.time()
                    if end_marker and end_marker in full_text:
                        self.drain(0.2)
                        break
            else:
                if (
                    end_marker is None
                    and (time.time() - last_rx) > idle_timeout
                ):
                    break
                if (
                    end_marker is not None
                    and (time.time() - last_rx) > 4.0
                    and "REGDUMP_BEGIN" in full_text
                ):
                    break

        return "".join(chunks)


def capture_via_uart(
    uart_spec: str, out_dir: Optional[Path]
) -> Tuple[str, str]:
    """Capture Cold-Boot-RO and Sysjump-RO register dumps over TCP UART."""
    host, port_s = uart_spec.rsplit(":", 1)
    client = EcUartClient(host, int(port_s))
    try:
        client.sock.sendall(b"\r\nchan 0\r\n")
        client.drain(0.3)

        client.sock.sendall(b"reboot hard ap-off-in-ro\r\n")
        time.sleep(3.5)
        client.drain(1.0)
        client.sock.sendall(b"\r\nchan 0\r\n")
        client.drain(0.5)

        cold_pre = client.send_cmd(
            "regdump pre",
            end_marker="REGDUMP_END|stage=PRE_INIT",
            timeout=90.0,
        )
        cold_post = client.send_cmd(
            "regdump post",
            end_marker="REGDUMP_END|stage=POST_INIT",
            timeout=90.0,
        )
        cold_diff = client.send_cmd(
            "regdump diff", end_marker="REGDIFF_END|", timeout=60.0
        )
        cold_combined = cold_pre + "\n" + cold_post + "\n" + cold_diff

        client.sock.sendall(b"sysjump rw\r\n")
        time.sleep(4.0)
        client.drain(1.0)
        client.sock.sendall(b"\r\nchan 0\r\n")
        client.drain(0.5)

        client.sock.sendall(b"sysjump ro\r\n")
        time.sleep(3.5)
        client.drain(1.0)
        client.sock.sendall(b"\r\nchan 0\r\n")
        client.drain(0.5)

        jump_pre = client.send_cmd(
            "regdump pre",
            end_marker="REGDUMP_END|stage=PRE_INIT",
            timeout=90.0,
        )
        jump_post = client.send_cmd(
            "regdump post",
            end_marker="REGDUMP_END|stage=POST_INIT",
            timeout=90.0,
        )
        jump_diff = client.send_cmd(
            "regdump diff", end_marker="REGDIFF_END|", timeout=60.0
        )
        jump_combined = jump_pre + "\n" + jump_post + "\n" + jump_diff

        client.sock.sendall(b"chan restore\r\n")
        client.drain(0.2)
    finally:
        client.close()

    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "cold_ro.txt").write_text(cold_combined, encoding="utf-8")
        (out_dir / "sysjump_ro.txt").write_text(jump_combined, encoding="utf-8")

    return cold_combined, jump_combined


def parse_regdump(text: str, target_stage: Optional[str] = None) -> RegDump:
    """Parse structured REGDUMP output for a given boot stage."""
    dump = RegDump()
    in_dump = False

    for raw_line in text.splitlines():
        line = raw_line.strip()
        m_begin = RE_BEGIN.search(line)
        if m_begin:
            stg = m_begin.group(1)
            if target_stage is None or stg == target_stage:
                dump = RegDump(
                    stage=stg,
                    image=m_begin.group(2),
                    jumped=int(m_begin.group(3)),
                    reset_flags=int(m_begin.group(4), 16),
                )
                in_dump = True
            continue

        if "REGDUMP_END|" in line and in_dump:
            break

        m_core = RE_CORE.search(line)
        if m_core:
            stg = m_core.group(1)
            if target_stage is None or stg == target_stage:
                for kv in m_core.group(2).split("|"):
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        try:
                            dump.core_regs[k] = int(v, 16)
                        except ValueError:
                            pass
            continue

        m_mod = RE_MOD.search(line)
        if m_mod:
            stg, name, tname, base_s, clk_s = m_mod.groups()
            if target_stage is None or stg == target_stage:
                dump.modules[name] = ModEntry(
                    stage=stg,
                    name=name,
                    type_name=tname,
                    base=int(base_s, 16),
                    clocked=int(clk_s),
                )
            continue

        m_reg = RE_REG.search(line)
        if m_reg:
            stg, mod, rname, off_s, sz_s, val_s, flags = m_reg.groups()
            if target_stage is not None and stg != target_stage:
                continue
            val_int: Optional[int] = None
            if val_s.startswith("0x") or val_s.startswith("0X"):
                try:
                    val_int = int(val_s, 16)
                except ValueError:
                    val_int = None
            entry = RegEntry(
                stage=stg,
                module=mod,
                name=rname,
                offset=int(off_s, 16),
                size=int(sz_s),
                val_str=val_s,
                val=val_int,
                flags=flags.strip(),
            )
            dump.regs[entry.key] = entry

    return dump


def compare_dumps(
    cold_pre: RegDump,
    cold_post: RegDump,
    jump_pre: RegDump,
    jump_post: RegDump,
    include_volatile: bool = False,
) -> str:
    """Format a Markdown report comparing Cold Boot vs. Sysjump snapshots."""
    lines: List[str] = []
    lines.append("# EC Hardware Register Audit Report (Cold Boot vs. Sysjump)")
    lines.append("")
    lines.append("## Snapshot Metadata")
    lines.append(
        f"- **Cold Boot (`PRE_INIT` / `POST_INIT`)**: "
        f"image=`{cold_post.image}`, jumped=`{cold_post.jumped}`, "
        f"reset=`0x{cold_post.reset_flags:08x}`, "
        f"registers=`{len(cold_post.regs)}`"
    )
    lines.append(
        f"- **Sysjump (`PRE_INIT` / `POST_INIT`)**: "
        f"image=`{jump_post.image}`, jumped=`{jump_post.jumped}`, "
        f"reset=`0x{jump_post.reset_flags:08x}`, "
        f"registers=`{len(jump_post.regs)}`"
    )
    lines.append("")

    clk_diffs = []
    for mod_name, c_mod in cold_post.modules.items():
        j_mod = jump_post.modules.get(mod_name)
        cp_mod = cold_pre.modules.get(mod_name)
        jp_mod = jump_pre.modules.get(mod_name)
        if not j_mod:
            continue
        c_pre_clk = cp_mod.clocked if cp_mod else c_mod.clocked
        j_pre_clk = jp_mod.clocked if jp_mod else j_mod.clocked
        if c_pre_clk != j_pre_clk or c_mod.clocked != j_mod.clocked:
            clk_diffs.append(
                (
                    mod_name,
                    c_mod.base,
                    c_pre_clk,
                    j_pre_clk,
                    c_mod.clocked,
                    j_mod.clocked,
                )
            )

    lines.append(f"## 1. Peripheral Clock Gating Mismatches ({len(clk_diffs)})")
    if clk_diffs:
        lines.append(
            "| Module | Base | Cold Pre | Jump Pre | Cold Post | Jump Post |"
        )
        lines.append("|---|---|---|---|---|---|")
        for mod_name, base, cpre, jpre, cpost, jpost in clk_diffs:
            lines.append(
                f"| `{mod_name}` | `0x{base:08x}` | `{cpre}` | `{jpre}` | "
                f"`{cpost}` | `{jpost}` |"
            )
    else:
        lines.append("No peripheral clock gating mismatches detected.")
    lines.append("")

    cat_a: List[
        Tuple[RegEntry, Optional[RegEntry], Optional[RegEntry], RegEntry]
    ] = []
    cat_b: List[
        Tuple[RegEntry, RegEntry, Optional[RegEntry], Optional[RegEntry]]
    ] = []

    all_keys = list(cold_post.regs.keys())
    for key in all_keys:
        c_post = cold_post.regs.get(key)
        j_post = jump_post.regs.get(key)
        c_pre = cold_pre.regs.get(key)
        j_pre = jump_pre.regs.get(key)

        if not c_post or not j_post:
            continue
        if c_post.is_skipped or j_post.is_skipped:
            continue
        if not include_volatile and c_post.is_volatile:
            continue

        if c_post.val != j_post.val:
            cat_a.append((c_post, c_pre, j_pre, j_post))
        elif c_pre and j_pre and not c_pre.is_skipped and not j_pre.is_skipped:
            if c_pre.val != j_pre.val:
                cat_b.append((c_pre, j_pre, c_post, j_post))

    lines.append(
        "## 2. Unsanitized Post-Init Register Leaks "
        f"(`Cold POST_INIT != Sysjump POST_INIT`): {len(cat_a)}"
    )
    lines.append(
        "These non-volatile registers remain in a different state *after* "
        "`HOOK_INIT` when entering via `sysjump` vs. cold boot:"
    )
    if cat_a:
        lines.append(
            "| Module | Register | Offset | Cold Pre | Sysjump Pre | "
            "Cold Post | Sysjump Post | XOR Delta |"
        )
        lines.append("|---|---|---|---|---|---|---|---|")
        for c_post, c_pre, j_pre, j_post in cat_a:
            w = c_post.size * 2
            cpre_s = (
                f"`0x{c_pre.val:0{w}x}`"
                if (c_pre and c_pre.val is not None)
                else "`-`"
            )
            jpre_s = (
                f"`0x{j_pre.val:0{w}x}`"
                if (j_pre and j_pre.val is not None)
                else "`-`"
            )
            delta = (c_post.val or 0) ^ (j_post.val or 0)
            lines.append(
                f"| `{c_post.module}` | `{c_post.name}` | "
                f"`0x{c_post.offset:04x}` | {cpre_s} | {jpre_s} | "
                f"`0x{c_post.val:0{w}x}` | `0x{j_post.val:0{w}x}` | "
                f"`0x{delta:0{w}x}` |"
            )
    else:
        lines.append("Zero post-init non-volatile register leaks found!")
    lines.append("")

    lines.append(
        "## 3. Pre-Init Entry Differences Sanitized by Init "
        f"(`Cold PRE_INIT != Sysjump PRE_INIT`, `POST_INIT` Equal): "
        f"{len(cat_b)}"
    )
    lines.append(
        "These registers entered the image dirty across `sysjump` (exposed to "
        "any early code running before driver init), but were subsequently "
        "re-initialized before `HOOK_INIT` completed:"
    )
    if cat_b:
        lines.append(
            "| Module | Register | Offset | Cold Pre | Sysjump Pre | "
            "Post-Init Value |"
        )
        lines.append("|---|---|---|---|---|---|")
        for c_pre, j_pre, c_post, _ in cat_b:
            w = c_pre.size * 2
            cpost_s = (
                f"`0x{c_post.val:0{w}x}`"
                if (c_post and c_post.val is not None)
                else "`-`"
            )
            lines.append(
                f"| `{c_pre.module}` | `{c_pre.name}` | "
                f"`0x{c_pre.offset:04x}` | `0x{c_pre.val:0{w}x}` | "
                f"`0x{j_pre.val:0{w}x}` | {cpost_s} |"
            )
    else:
        lines.append("Zero pre-init-only differences found.")
    lines.append("")

    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    """CLI entry point for EC register audit comparison."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--uart",
        help="Capture Cold-Boot and Sysjump dumps live over TCP host:port",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        help="Directory to save raw UART captures when using --uart",
    )
    parser.add_argument(
        "--cold-pre",
        type=Path,
        help="File containing Cold Boot PRE_INIT regdump",
    )
    parser.add_argument(
        "--cold-post",
        type=Path,
        help="File containing Cold Boot POST_INIT regdump",
    )
    parser.add_argument(
        "--jump-pre",
        type=Path,
        help="File containing Sysjump PRE_INIT regdump",
    )
    parser.add_argument(
        "--jump-post",
        type=Path,
        help="File containing Sysjump POST_INIT regdump",
    )
    parser.add_argument(
        "--cold",
        type=Path,
        help="Single file with PRE_INIT and POST_INIT dumps from Cold Boot",
    )
    parser.add_argument(
        "--sysjump",
        type=Path,
        help="Single file with PRE_INIT and POST_INIT dumps from Sysjump",
    )
    parser.add_argument(
        "--include-volatile",
        action="store_true",
        help="Include volatile registers (counters, pin inputs, ADC) in diff",
    )
    parser.add_argument(
        "-o", "--output", type=Path, help="Write markdown report to file"
    )
    args = parser.parse_args(argv)

    if args.uart:
        cold_txt, jump_txt = capture_via_uart(args.uart, args.out_dir)
        cold_pre = parse_regdump(cold_txt, "PRE_INIT")
        cold_post = parse_regdump(cold_txt, "POST_INIT")
        jump_pre = parse_regdump(jump_txt, "PRE_INIT")
        jump_post = parse_regdump(jump_txt, "POST_INIT")
    elif args.cold and args.sysjump:
        cold_txt = args.cold.read_text(encoding="utf-8", errors="replace")
        jump_txt = args.sysjump.read_text(encoding="utf-8", errors="replace")
        cold_pre = parse_regdump(cold_txt, "PRE_INIT")
        cold_post = parse_regdump(cold_txt, "POST_INIT")
        if not cold_post.regs:
            cold_post = parse_regdump(cold_txt)
        jump_pre = parse_regdump(jump_txt, "PRE_INIT")
        jump_post = parse_regdump(jump_txt, "POST_INIT")
        if not jump_post.regs:
            jump_post = parse_regdump(jump_txt)
    elif args.cold_post and args.jump_post:
        cold_post = parse_regdump(
            args.cold_post.read_text(encoding="utf-8", errors="replace")
        )
        jump_post = parse_regdump(
            args.jump_post.read_text(encoding="utf-8", errors="replace")
        )
        cold_pre = RegDump()
        jump_pre = RegDump()
        if args.cold_pre:
            cold_pre = parse_regdump(
                args.cold_pre.read_text(encoding="utf-8", errors="replace")
            )
        if args.jump_pre:
            jump_pre = parse_regdump(
                args.jump_pre.read_text(encoding="utf-8", errors="replace")
            )
    else:
        parser.error(
            "Provide --uart, (--cold and --sysjump), or "
            "(--cold-post and --jump-post)"
        )
        return 1

    report = compare_dumps(
        cold_pre,
        cold_post,
        jump_pre,
        jump_post,
        include_volatile=args.include_volatile,
    )
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
