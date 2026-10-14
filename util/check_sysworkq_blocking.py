#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Static ELF call-graph analyzer for Zephyr EC sysworkq non-blocking audit.

Extracts all registered hook routines (DECLARE_HOOK / .zephyr_shim_hook_*) and
deferred routines (DECLARE_DEFERRED / *_workq_wrapper / *_work_data) from a
Zephyr EC ELF binary, builds the static function call graph, and reports any
call chains that can reach blocking kernel or shim primitives on k_sys_work_q.
"""

import argparse
from collections import deque
from dataclasses import asdict
from dataclasses import dataclass
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple


try:
    from elftools.elf.elffile import ELFFile as _PyElfFile
except ImportError:  # pragma: no cover
    _PyElfFile = None


_STT_MAP: Dict[int, str] = {
    0: "STT_NOTYPE",
    1: "STT_OBJECT",
    2: "STT_FUNC",
    3: "STT_SECTION",
    4: "STT_FILE",
}


class _MiniSymbol:
    """Minimal pure-Python ELF symbol matching pyelftools Symbol interface."""

    def __init__(self, name: str, st_value: int, st_size: int, st_type: str):
        self.name = name
        self.entry = self
        self.st_value = st_value
        self.st_size = st_size
        self._info = {"type": st_type}

    def __getitem__(self, key: str) -> Any:
        if key == "st_info":
            return self._info
        raise KeyError(key)


class _MiniSection:
    """Minimal pure-Python ELF section matching pyelftools Section interface."""

    def __init__(self, name: str, sh_addr: int, raw_data: bytes):
        self.name = name
        self.header = {"sh_addr": sh_addr, "sh_size": len(raw_data)}
        self._data = raw_data

    def data(self) -> bytes:
        """Returns raw section bytes."""
        return self._data


class _MiniSymtabSection(_MiniSection):
    """Minimal pure-Python ELF .symtab section."""

    def __init__(
        self,
        name: str,
        sh_addr: int,
        raw_data: bytes,
        symbols: List[_MiniSymbol],
    ):
        super().__init__(name, sh_addr, raw_data)
        self._symbols = symbols

    def iter_symbols(self) -> Iterable[_MiniSymbol]:
        """Yields parsed symbol entries."""
        return iter(self._symbols)


class _MiniELFFile:
    """Pure-Python ELF32/ELF64 reader fallback when pyelftools is absent."""

    def __init__(self, stream: Any):
        buf = stream.read()
        if len(buf) < 52 or buf[:4] != b"\x7fELF":
            raise ValueError("Not a valid ELF file")

        self.elfclass = 32 if buf[4] == 1 else 64
        self.little_endian = buf[5] == 1
        endian = "<" if self.little_endian else ">"

        if self.elfclass == 32:
            (
                e_shoff,
                _flags,
                _ehsize,
                _phentsize,
                _phnum,
                e_shentsize,
                e_shnum,
                e_shstrndx,
            ) = struct.unpack_from(f"{endian}IIHHHHHH", buf, 32)
            sh_fmt = f"{endian}10I"
        else:
            (
                e_shoff,
                _flags,
                _ehsize,
                _phentsize,
                _phnum,
                e_shentsize,
                e_shnum,
                e_shstrndx,
            ) = struct.unpack_from(f"{endian}QIHHHHHH", buf, 40)
            sh_fmt = f"{endian}IIQQQQIIQQ"

        raw_shdrs = []
        for idx in range(e_shnum):
            off = e_shoff + idx * e_shentsize
            raw_shdrs.append(struct.unpack_from(sh_fmt, buf, off))

        shstrtab = b""
        if 0 <= e_shstrndx < len(raw_shdrs):
            s_off = raw_shdrs[e_shstrndx][4]
            s_sz = raw_shdrs[e_shstrndx][5]
            shstrtab = buf[s_off : s_off + s_sz]

        self._sections: List[_MiniSection] = []
        self._by_name: Dict[str, _MiniSection] = {}

        for shdr in raw_shdrs:
            name_off = shdr[0]
            sh_type = shdr[1]
            sh_addr = shdr[3]
            sh_offset = shdr[4]
            sh_size = shdr[5]
            sh_link = shdr[6]
            sh_entsize = shdr[9]

            end_name = shstrtab.find(b"\x00", name_off)
            sec_name = (
                shstrtab[name_off:end_name].decode("utf-8", errors="replace")
                if end_name != -1
                else ""
            )
            sec_bytes = (
                buf[sh_offset : sh_offset + sh_size]
                if sh_type != 8 and sh_offset + sh_size <= len(buf)
                else b"\x00" * sh_size
            )

            if sh_type == 2 and 0 <= sh_link < len(raw_shdrs):  # SHT_SYMTAB
                str_shdr = raw_shdrs[sh_link]
                strtab = buf[str_shdr[4] : str_shdr[4] + str_shdr[5]]
                syms: List[_MiniSymbol] = []
                ent_sz = sh_entsize or (16 if self.elfclass == 32 else 24)
                for s_off in range(0, len(sec_bytes), ent_sz):
                    if s_off + ent_sz > len(sec_bytes):
                        break
                    if self.elfclass == 32:
                        st_name, st_value, st_size, st_info, _other, _shndx = (
                            struct.unpack_from(
                                f"{endian}IIIBBH", sec_bytes, s_off
                            )
                        )
                    else:
                        st_name, st_info, _other, _shndx, st_value, st_size = (
                            struct.unpack_from(
                                f"{endian}IBBHQQ", sec_bytes, s_off
                            )
                        )
                    end_s = strtab.find(b"\x00", st_name)
                    sym_name = (
                        strtab[st_name:end_s].decode("utf-8", errors="replace")
                        if end_s != -1
                        else ""
                    )
                    st_type = _STT_MAP.get(st_info & 0xF, "STT_NOTYPE")
                    syms.append(
                        _MiniSymbol(sym_name, st_value, st_size, st_type)
                    )
                sec_obj: _MiniSection = _MiniSymtabSection(
                    sec_name, sh_addr, sec_bytes, syms
                )
            else:
                sec_obj = _MiniSection(sec_name, sh_addr, sec_bytes)

            self._sections.append(sec_obj)
            if sec_name:
                self._by_name[sec_name] = sec_obj

    def get_section_by_name(self, name: str) -> Optional[_MiniSection]:
        """Returns section matching name or None."""
        return self._by_name.get(name)

    def iter_sections(self) -> Iterable[_MiniSection]:
        """Yields all ELF sections."""
        return iter(self._sections)


ELFFile = _PyElfFile or _MiniELFFile


_ARM_THUMB_BIT_MASK = ~1

# Unconditional blocking or sleep primitives that must never run on sysworkq.
DEFAULT_BLOCKING_SINKS: Tuple[str, ...] = (
    "crec_usleep",
    "k_sleep",
    "z_impl_k_sleep",
    "k_usleep",
    "z_impl_k_usleep",
    "task_wait_event",
    "task_wait_event_mask",
    "k_work_cancel_sync",
    "k_work_cancel_delayable_sync",
    "k_work_flush",
    "k_work_flush_delayable",
    "k_thread_join",
    "z_impl_k_thread_join",
)

# Primitives that accept a k_timeout_t and block when timeout != K_NO_WAIT.
TIMEOUT_BLOCKING_SINKS: Tuple[str, ...] = (
    "k_mutex_lock",
    "z_impl_k_mutex_lock",
    "k_sem_take",
    "z_impl_k_sem_take",
    "k_condvar_wait",
    "z_impl_k_condvar_wait",
    "k_event_wait",
    "z_impl_k_event_wait",
    "k_event_wait_all",
    "z_impl_k_event_wait_all",
    "k_msgq_get",
    "z_impl_k_msgq_get",
    "k_msgq_put",
    "z_impl_k_msgq_put",
    "k_poll",
    "z_impl_k_poll",
)

# Audit/assertion helpers that reference blocking functions or symbols internally
# and should not themselves be traversed as blocking paths.
DEFAULT_IGNORED_FUNCTIONS: Tuple[str, ...] = (
    "sysworkq_audit_check_non_blocking",
    "report_violation",
    "assert_post_action",
    "zassert_fail",
    "zassert_post_action",
    "z_zassert",
    "__assert_fail",
    "k_sys_fatal_error_handler",
)

_HOOK_START_RE = re.compile(r"^_zephyr_shim_hook_(HOOK_[A-Z0-9_]+)_list_start$")
_CROS_HOOK_SYM_RE = re.compile(r"^_cros_hook_(HOOK_[A-Z0-9_]+)_(.+)$")
_OBJDUMP_FUNC_HEADER_RE = re.compile(r"^[0-9a-fA-F]+\s+<([^+>]+)>:\s*$")
_OBJDUMP_CALL_TARGET_RE = re.compile(r"<([^+>\s]+)(?:\+0x[0-9a-fA-F]+)?>")


@dataclass(frozen=True)
class SysworkqRoot:
    """Represents a root function dispatched on k_sys_work_q."""

    kind: str  # "hook" or "deferred"
    name: str  # Function symbol name
    source: str  # Hook type (e.g. HOOK_TICK) or "DECLARE_DEFERRED"
    address: int


@dataclass(frozen=True)
class BlockingViolation:
    """Represents a static call path from a sysworkq root to a blocking sink."""

    root_kind: str
    root_name: str
    root_source: str
    sink_name: str
    call_chain: List[str]


def strip_lto_mangling(name: str) -> str:
    """Strips compiler optimization suffixes (.lto_priv, .constprop, .isra, .part)."""
    if not name:
        return name
    return re.sub(r"\.(lto_priv|constprop|isra|part|cold|hot)\b.*", "", name)


def _find_section_for_vaddr(elf: Any, vaddr: int) -> Optional[Any]:
    """Finds the ELF section containing the given virtual address."""
    for sec in elf.iter_sections():
        sh_addr = sec.header["sh_addr"]
        sh_size = sec.header["sh_size"]
        if sh_size > 0 and sh_addr <= vaddr < sh_addr + sh_size:
            return sec
    return None


def extract_sysworkq_roots(
    elf: Any,
) -> Tuple[List[SysworkqRoot], Dict[int, str]]:
    """Extracts all DECLARE_HOOK and DECLARE_DEFERRED roots from an ELFFile.

    Args:
        elf: An open pyelftools ELFFile instance.

    Returns:
        A tuple of (sorted list of SysworkqRoot, function address-to-name map).
    """
    symtab = elf.get_section_by_name(".symtab")
    if not symtab:
        raise ValueError("ELF file does not contain a .symtab section")

    is_64bit = elf.elfclass == 64
    endian = "<" if elf.little_endian else ">"
    ptr_size = 8 if is_64bit else 4
    ptr_fmt = f"{endian}Q" if is_64bit else f"{endian}I"
    hook_info_size = 16 if is_64bit else 8

    symbols: Dict[str, int] = {}
    func_by_name: Dict[str, int] = {}
    addr_to_func: Dict[int, str] = {}
    raw_symbols: List[Tuple[str, int, int, str]] = []

    for sym in symtab.iter_symbols():
        name = sym.name
        if not name or name.startswith("$"):
            continue
        vaddr = sym.entry.st_value
        size = sym.entry.st_size
        st_type = sym.entry["st_info"]["type"]
        clean = strip_lto_mangling(name)
        symbols[name] = vaddr
        symbols.setdefault(clean, vaddr)
        raw_symbols.append((clean, vaddr, size, st_type))
        if st_type == "STT_FUNC" and vaddr > 0:
            norm_addr = vaddr & _ARM_THUMB_BIT_MASK if not is_64bit else vaddr
            func_by_name[clean] = norm_addr
            addr_to_func[norm_addr] = clean
            addr_to_func[vaddr] = clean

    roots: Dict[Tuple[str, str, str], SysworkqRoot] = {}

    # 1. Extract hooks from _zephyr_shim_hook_<HOOK_TYPE>_list_start .. _end
    for sym_name, start_addr in list(symbols.items()):
        m = _HOOK_START_RE.match(sym_name)
        if not m:
            continue
        hook_type = m.group(1)
        end_sym = f"_zephyr_shim_hook_{hook_type}_list_end"
        end_addr = symbols.get(end_sym)
        if end_addr is None or end_addr <= start_addr:
            continue

        sec = _find_section_for_vaddr(elf, start_addr)
        if not sec:
            continue
        sec_start = sec.header["sh_addr"]
        sec_data = sec.data()

        for addr in range(start_addr, end_addr, hook_info_size):
            offset = addr - sec_start
            if offset < 0 or offset + ptr_size > len(sec_data):
                break
            routine_ptr = struct.unpack_from(ptr_fmt, sec_data, offset)[0]
            norm_ptr = (
                routine_ptr & _ARM_THUMB_BIT_MASK
                if not is_64bit
                else routine_ptr
            )
            fn_name = addr_to_func.get(norm_ptr) or addr_to_func.get(
                routine_ptr
            )
            if fn_name:
                key = ("hook", fn_name, hook_type)
                roots[key] = SysworkqRoot(
                    kind="hook",
                    name=fn_name,
                    source=hook_type,
                    address=norm_ptr,
                )

    # 2. Fallback: check _cros_hook_<HOOK_TYPE>_<routine> symbols in .symtab
    for clean_name, vaddr, _size, st_type in raw_symbols:
        if st_type not in ("STT_OBJECT", "STT_NOTYPE"):
            continue
        m = _CROS_HOOK_SYM_RE.match(clean_name)
        if not m:
            continue
        hook_type, routine_name = m.group(1), m.group(2)
        fn_addr = func_by_name.get(routine_name, 0)
        if routine_name in func_by_name:
            key = ("hook", routine_name, hook_type)
            roots.setdefault(
                key,
                SysworkqRoot(
                    kind="hook",
                    name=routine_name,
                    source=hook_type,
                    address=fn_addr,
                ),
            )

    # 3. Extract DECLARE_DEFERRED routines (*_workq_wrapper and *_work_data)
    for clean_name, vaddr, _size, st_type in raw_symbols:
        if st_type == "STT_FUNC" and clean_name.endswith("_workq_wrapper"):
            base_name = clean_name[: -len("_workq_wrapper")]
            target_name = base_name if base_name in func_by_name else clean_name
            target_addr = func_by_name.get(target_name, vaddr)
            key = ("deferred", target_name, "DECLARE_DEFERRED")
            roots[key] = SysworkqRoot(
                kind="deferred",
                name=target_name,
                source="DECLARE_DEFERRED",
                address=target_addr,
            )
        elif st_type in ("STT_OBJECT", "STT_NOTYPE") and clean_name.endswith(
            "_work_data"
        ):
            base_name = clean_name[: -len("_work_data")]
            if base_name in func_by_name:
                key = ("deferred", base_name, "DECLARE_DEFERRED")
                roots.setdefault(
                    key,
                    SysworkqRoot(
                        kind="deferred",
                        name=base_name,
                        source="DECLARE_DEFERRED",
                        address=func_by_name[base_name],
                    ),
                )

    sorted_roots = sorted(
        roots.values(), key=lambda r: (r.kind, r.source, r.name)
    )
    return sorted_roots, addr_to_func


def parse_objdump_call_graph(disasm_text: str) -> Dict[str, Set[str]]:
    """Parses `objdump -d` output into a caller -> set(callees) adjacency map.

    Args:
        disasm_text: Output string from `objdump -d` or `llvm-objdump -d`.

    Returns:
        Dictionary mapping caller function name to set of callee function names.
    """
    call_graph: Dict[str, Set[str]] = {}
    current_func: Optional[str] = None

    for line in disasm_text.splitlines():
        header_match = _OBJDUMP_FUNC_HEADER_RE.match(line)
        if header_match:
            current_func = strip_lto_mangling(header_match.group(1))
            call_graph.setdefault(current_func, set())
            continue

        if current_func is None or "<" not in line:
            continue

        for target_match in _OBJDUMP_CALL_TARGET_RE.finditer(line):
            callee = strip_lto_mangling(target_match.group(1))
            if callee and callee != current_func:
                call_graph[current_func].add(callee)

    return call_graph


def extract_x86_calls_from_elf(
    elf: Any, addr_to_func: Dict[int, str]
) -> Dict[str, Set[str]]:
    """Fallback binary call-graph extractor for x86/x86_64 ELF executables."""
    symtab = elf.get_section_by_name(".symtab")
    if not symtab:
        return {}

    call_graph: Dict[str, Set[str]] = {}
    for sym in symtab.iter_symbols():
        if sym.entry["st_info"]["type"] != "STT_FUNC":
            continue
        fn_name = strip_lto_mangling(sym.name)
        raw_vaddr = sym.entry.st_value
        vaddr = (
            raw_vaddr & _ARM_THUMB_BIT_MASK if elf.elfclass == 32 else raw_vaddr
        )
        size = sym.entry.st_size
        if not fn_name or vaddr == 0 or size == 0:
            continue

        call_graph.setdefault(fn_name, set())
        sec = _find_section_for_vaddr(elf, vaddr)
        if not sec:
            continue
        sec_start = sec.header["sh_addr"]
        data = sec.data()
        start_off = vaddr - sec_start
        end_off = min(start_off + size, len(data))
        fn_bytes = data[start_off:end_off]

        # Scan for near relative CALL (0xE8 rel32) and JMP (0xE9 rel32)
        for idx in range(0, max(0, len(fn_bytes) - 4)):
            opcode = fn_bytes[idx]
            if opcode in (0xE8, 0xE9):
                rel32 = struct.unpack_from("<i", fn_bytes, idx + 1)[0]
                target_addr = (vaddr + idx + 5 + rel32) & 0xFFFFFFFFFFFFFFFF
                callee = addr_to_func.get(target_addr)
                if callee and callee != fn_name:
                    call_graph[fn_name].add(callee)

    return call_graph


def build_call_graph(
    elf_path: str,
    elf: Any,
    addr_to_func: Dict[int, str],
    objdump_bin: Optional[str] = None,
) -> Dict[str, Set[str]]:
    """Builds the static call graph for an ELF file using objdump or fallback."""
    candidates = (
        [objdump_bin]
        if objdump_bin
        else ["llvm-objdump", "objdump", "arm-none-eabi-objdump"]
    )
    resolved_objdump = None
    for cand in candidates:
        if cand and shutil.which(cand):
            resolved_objdump = cand
            break

    if resolved_objdump:
        proc = subprocess.run(
            [resolved_objdump, "-d", elf_path],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode == 0 and proc.stdout:
            return parse_objdump_call_graph(proc.stdout)

    return extract_x86_calls_from_elf(elf, addr_to_func)


def find_blocking_violations(
    roots: Iterable[SysworkqRoot],
    call_graph: Dict[str, Set[str]],
    blocking_sinks: Set[str],
    ignored_functions: Optional[Set[str]] = None,
) -> List[BlockingViolation]:
    """Finds shortest static call chains from sysworkq roots to blocking sinks.

    Args:
        roots: Iterable of SysworkqRoot objects to start BFS from.
        call_graph: Caller -> set(callees) dictionary.
        blocking_sinks: Set of function names considered blocking violations.
        ignored_functions: Optional set of functions to exclude from traversal.

    Returns:
        Sorted list of BlockingViolation instances.
    """
    ignored = ignored_functions or set()
    violations: List[BlockingViolation] = []

    for root in roots:
        if root.name in ignored:
            continue

        # For DECLARE_DEFERRED with latency audit enabled, the k_work handler is
        # <routine>_workq_wrapper which calls zephyr_shim_run_deferred -> routine.
        # Starting at root.name directly analyzes the user handler itself.
        queue: deque[List[str]] = deque([[root.name]])
        visited: Set[str] = {root.name}
        found_sinks: Set[str] = set()

        while queue:
            path = queue.popleft()
            current = path[-1]

            if current in blocking_sinks and len(path) > 1:
                if current not in found_sinks:
                    found_sinks.add(current)
                    violations.append(
                        BlockingViolation(
                            root_kind=root.kind,
                            root_name=root.name,
                            root_source=root.source,
                            sink_name=current,
                            call_chain=path,
                        )
                    )
                continue

            for callee in sorted(call_graph.get(current, ())):
                if callee in ignored or callee in visited:
                    continue
                visited.add(callee)
                queue.append(path + [callee])

    violations.sort(
        key=lambda v: (v.root_kind, v.root_source, v.root_name, v.sink_name)
    )
    return violations


def analyze_elf(
    elf_path: str,
    include_timeout_sinks: bool = False,
    allowlist: Optional[Set[str]] = None,
    objdump_bin: Optional[str] = None,
) -> Tuple[List[SysworkqRoot], List[BlockingViolation]]:
    """Analyzes a Zephyr ELF file for sysworkq blocking call chains."""
    sinks: Set[str] = set(DEFAULT_BLOCKING_SINKS)
    if include_timeout_sinks:
        sinks.update(TIMEOUT_BLOCKING_SINKS)

    ignored: Set[str] = set(DEFAULT_IGNORED_FUNCTIONS)
    if allowlist:
        ignored.update(allowlist)

    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        roots, addr_to_func = extract_sysworkq_roots(elf)
        call_graph = build_call_graph(
            elf_path, elf, addr_to_func, objdump_bin=objdump_bin
        )

    violations = find_blocking_violations(
        roots=roots,
        call_graph=call_graph,
        blocking_sinks=sinks,
        ignored_functions=ignored,
    )
    return roots, violations


def main(argv: Optional[List[str]] = None) -> int:
    """CLI entrypoint for check_sysworkq_blocking."""
    parser = argparse.ArgumentParser(
        description=(
            "Statically audit Zephyr EC ELF files for hooks and deferred "
            "routines that call blocking primitives on sysworkq."
        )
    )
    parser.add_argument(
        "elf_files",
        nargs="+",
        help="Path(s) to Zephyr ELF file(s) (e.g. zephyr.elf, zephyr.ro.elf).",
    )
    parser.add_argument(
        "--include-timeout-primitives",
        action="store_true",
        help=(
            "Also flag timeout-parameterized primitives (k_mutex_lock, "
            "k_sem_take, k_condvar_wait, k_event_wait, k_msgq_*, k_poll)."
        ),
    )
    parser.add_argument(
        "--allowlist",
        action="append",
        default=[],
        help="Function name(s) (comma-separated) to exclude from violations.",
    )
    parser.add_argument(
        "--objdump",
        default=None,
        help="Explicit objdump binary to use for disassembly.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON report.",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Exit with status 0 even if blocking call chains are found.",
    )

    args = parser.parse_args(argv)

    allow_set: Set[str] = set()
    for item in args.allowlist:
        for fn in item.split(","):
            fn = fn.strip()
            if fn:
                allow_set.add(fn)

    all_reports: Dict[str, Any] = {}
    total_violations = 0

    for elf_path in args.elf_files:
        if not os.path.isfile(elf_path):
            print(f"error: ELF file not found: {elf_path}", file=sys.stderr)
            return 2

        roots, violations = analyze_elf(
            elf_path=elf_path,
            include_timeout_sinks=args.include_timeout_primitives,
            allowlist=allow_set,
            objdump_bin=args.objdump,
        )
        total_violations += len(violations)
        all_reports[elf_path] = {
            "roots_analyzed": len(roots),
            "roots": [asdict(r) for r in roots],
            "violations": [asdict(v) for v in violations],
        }

        if not args.json:
            print(
                f"{elf_path}: analyzed {len(roots)} sysworkq roots "
                f"({sum(1 for r in roots if r.kind == 'hook')} hooks, "
                f"{sum(1 for r in roots if r.kind == 'deferred')} deferred), "
                f"found {len(violations)} blocking path(s)."
            )
            for v in violations:
                chain_str = " -> ".join(v.call_chain)
                print(f"  [{v.root_kind}:{v.root_source}] {chain_str}")

    if args.json:
        print(json.dumps(all_reports, indent=2))

    if total_violations > 0 and not args.warn_only:
        return 1
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
