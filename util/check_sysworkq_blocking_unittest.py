#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unit tests for check_sysworkq_blocking.py."""

import io
import json
import struct
import tempfile
import unittest
from unittest import mock

import check_sysworkq_blocking


# pylint: disable=protected-access


class _FakeSymbol:
    """Minimal mock for a pyelftools Symbol."""

    def __init__(
        self,
        name: str,
        st_value: int,
        st_size: int = 16,
        st_type: str = "STT_FUNC",
    ):
        self.name = name
        self.entry = mock.MagicMock()
        self.entry.st_value = st_value
        self.entry.st_size = st_size
        self.entry.__getitem__.side_effect = lambda k: (
            {"type": st_type} if k == "st_info" else None
        )


class _FakeSection:
    """Minimal mock for a pyelftools Section."""

    def __init__(self, sh_addr: int, data_bytes: bytes):
        self.header = {"sh_addr": sh_addr, "sh_size": len(data_bytes)}
        self._data = data_bytes

    def data(self) -> bytes:
        """Returns raw section bytes."""
        return self._data


class _FakeSymtab:
    """Minimal mock for a pyelftools SymbolTableSection."""

    def __init__(self, symbols):
        self._symbols = symbols

    def iter_symbols(self):
        """Yields fake symbols."""
        return iter(self._symbols)


class _FakeELFFile:
    """Minimal mock for a pyelftools ELFFile."""

    def __init__(
        self,
        symbols,
        sections,
        elfclass: int = 32,
        little_endian: bool = True,
    ):
        self._symtab = _FakeSymtab(symbols) if symbols is not None else None
        self._sections = sections
        self.elfclass = elfclass
        self.little_endian = little_endian

    def get_section_by_name(self, name: str):
        """Returns fake .symtab section if requested."""
        if name == ".symtab":
            return self._symtab
        return None

    def iter_sections(self):
        """Yields fake sections."""
        return iter(self._sections)


class TestCheckSysworkqBlocking(unittest.TestCase):
    """Unit tests for static sysworkq call-graph analysis."""

    def test_strip_lto_mangling(self):
        """Verify compiler optimization suffixes are stripped cleanly."""
        self.assertEqual(check_sysworkq_blocking.strip_lto_mangling(""), "")
        self.assertEqual(
            check_sysworkq_blocking.strip_lto_mangling("led_tick.lto_priv.0"),
            "led_tick",
        )
        self.assertEqual(
            check_sysworkq_blocking.strip_lto_mangling("foo.constprop.12"),
            "foo",
        )
        self.assertEqual(
            check_sysworkq_blocking.strip_lto_mangling("bar.isra.0.part.1"),
            "bar",
        )
        self.assertEqual(
            check_sysworkq_blocking.strip_lto_mangling("normal_func"),
            "normal_func",
        )

    def test_parse_objdump_call_graph(self):
        """Verify objdump disassembly parser extracts direct and tail calls."""
        disasm = """
08001000 <hook_tick_handler.lto_priv.0>:
 8001000:	b508      	push	{r3, lr}
 8001002:	f7ff fffe 	bl	8001010 <helper_func>
 8001006:	e7fe      	b.n	8001002 <hook_tick_handler.lto_priv.0+0x2>
 8001008:	bd08      	pop	{r3, pc}

08001010 <helper_func>:
 8001010:	f000 b804 	b.w	8002000 <crec_usleep>
"""
        graph = check_sysworkq_blocking.parse_objdump_call_graph(disasm)
        self.assertEqual(graph["hook_tick_handler"], {"helper_func"})
        self.assertEqual(graph["helper_func"], {"crec_usleep"})

    def test_find_blocking_violations_bfs_and_allowlist(self):
        """Verify shortest-path BFS finds blocking chains and honors allowlist."""
        roots = [
            check_sysworkq_blocking.SysworkqRoot(
                kind="hook",
                name="my_tick_hook",
                source="HOOK_TICK",
                address=0x1000,
            ),
            check_sysworkq_blocking.SysworkqRoot(
                kind="deferred",
                name="my_deferred",
                source="DECLARE_DEFERRED",
                address=0x2000,
            ),
            check_sysworkq_blocking.SysworkqRoot(
                kind="deferred",
                name="ignored_root",
                source="DECLARE_DEFERRED",
                address=0x3000,
            ),
        ]
        call_graph = {
            "my_tick_hook": {"mid_step", "safe_step"},
            "safe_step": {"my_tick_hook"},  # Cycle must not loop infinitely
            "mid_step": {"crec_usleep"},
            "my_deferred": {"allowed_helper"},
            "allowed_helper": {"task_wait_event"},
            "ignored_root": {"k_sleep"},
        }
        sinks = set(check_sysworkq_blocking.DEFAULT_BLOCKING_SINKS)

        violations = check_sysworkq_blocking.find_blocking_violations(
            roots=roots,
            call_graph=call_graph,
            blocking_sinks=sinks,
            ignored_functions={"allowed_helper", "ignored_root"},
        )
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].root_name, "my_tick_hook")
        self.assertEqual(violations[0].sink_name, "crec_usleep")
        self.assertEqual(
            violations[0].call_chain,
            ["my_tick_hook", "mid_step", "crec_usleep"],
        )

    def test_extract_sysworkq_roots_and_x86_fallback(self):
        """Verify hook and deferred root extraction and x86 CALL decoding."""
        # .rodata section at 0x1000 containing one 32-bit zephyr_shim_hook_info:
        # routine = 0x2001 (Thumb bit set for 0x2000), priority = 1000
        rodata_bytes = struct.pack("<IHxx", 0x2001, 1000)
        rodata_sec = _FakeSection(0x1000, rodata_bytes)

        # .text section at 0x2000:
        # 0x2000 (hook_fn, size 8): E8 0B 00 00 00 (CALL 0x2010 <crec_usleep>)
        # 0x2010 (crec_usleep, size 4): C3 (RET)
        # 0x2020 (def_fn, size 8): E9 EB FF FF FF (JMP 0x2010 <crec_usleep>)
        # 0x2030 (def_fn_workq_wrapper, size 4): C3
        text_bytes = bytearray(0x40)
        text_bytes[0x00:0x05] = b"\xe8" + struct.pack("<i", 0x2010 - 0x2005)
        text_bytes[0x10] = 0xC3
        text_bytes[0x20:0x25] = b"\xe9" + struct.pack("<i", 0x2010 - 0x2025)
        text_bytes[0x30] = 0xC3
        text_sec = _FakeSection(0x2000, bytes(text_bytes))

        symbols = [
            _FakeSymbol(
                "_zephyr_shim_hook_HOOK_TICK_list_start",
                0x1000,
                0,
                "STT_NOTYPE",
            ),
            _FakeSymbol(
                "_zephyr_shim_hook_HOOK_TICK_list_end", 0x1008, 0, "STT_NOTYPE"
            ),
            _FakeSymbol(
                "_cros_hook_HOOK_INIT_init_fn", 0x1010, 8, "STT_OBJECT"
            ),
            _FakeSymbol("hook_fn", 0x2001, 8, "STT_FUNC"),
            _FakeSymbol("init_fn", 0x2018, 4, "STT_FUNC"),
            _FakeSymbol("crec_usleep", 0x2010, 4, "STT_FUNC"),
            _FakeSymbol("def_fn", 0x2020, 8, "STT_FUNC"),
            _FakeSymbol("def_fn_workq_wrapper", 0x2030, 4, "STT_FUNC"),
            _FakeSymbol("def2_fn", 0x2038, 4, "STT_FUNC"),
            _FakeSymbol("def2_fn_work_data", 0x3000, 32, "STT_OBJECT"),
        ]

        fake_elf = _FakeELFFile(symbols, [rodata_sec, text_sec], elfclass=32)
        roots, addr_to_func = check_sysworkq_blocking.extract_sysworkq_roots(
            fake_elf
        )
        root_tuples = [(r.kind, r.name, r.source) for r in roots]
        self.assertEqual(
            root_tuples,
            [
                ("deferred", "def2_fn", "DECLARE_DEFERRED"),
                ("deferred", "def_fn", "DECLARE_DEFERRED"),
                ("hook", "init_fn", "HOOK_INIT"),
                ("hook", "hook_fn", "HOOK_TICK"),
            ],
        )

        x86_graph = check_sysworkq_blocking.extract_x86_calls_from_elf(
            fake_elf, addr_to_func
        )
        self.assertEqual(x86_graph["hook_fn"], {"crec_usleep"})
        self.assertEqual(x86_graph["def_fn"], {"crec_usleep"})

    def test_extract_sysworkq_roots_missing_symtab(self):
        """Verify missing .symtab raises ValueError."""
        fake_elf = _FakeELFFile(None, [])
        with self.assertRaises(ValueError):
            check_sysworkq_blocking.extract_sysworkq_roots(fake_elf)

    def test_mini_elffile_and_analyze_elf_end_to_end(self):
        """Verify _MiniELFFile parses a synthetic ELF32 image end-to-end."""
        with self.assertRaises(ValueError):
            check_sysworkq_blocking._MiniELFFile(io.BytesIO(b"not an elf"))

        # Build a minimal valid 32-bit little-endian ELF with:
        # Sec 0: NULL
        # Sec 1: .shstrtab
        # Sec 2: .strtab
        # Sec 3: .symtab
        # Sec 4: .text (at vaddr 0x2000)
        shstrtab = b"\x00.shstrtab\x00.strtab\x00.symtab\x00.text\x00"
        strtab = b"\x00my_def\x00my_def_workq_wrapper\x00crec_usleep\x00"
        # .text: 0x2000 my_def calls 0x2010 crec_usleep via E8 0B 00 00 00
        text = bytearray(0x20)
        text[0:5] = b"\xe8" + struct.pack("<i", 0x2010 - 0x2005)
        text[0x10] = 0xC3

        # ELF32_Sym: st_name, st_value, st_size, st_info, st_other, st_shndx
        # STT_FUNC = 2
        sym_null = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)
        sym_my_def = struct.pack("<IIIBBH", 1, 0x2000, 8, 2, 0, 4)
        sym_wrapper = struct.pack("<IIIBBH", 8, 0x2008, 4, 2, 0, 4)
        sym_sleep = struct.pack("<IIIBBH", 29, 0x2010, 4, 2, 0, 4)
        symtab = sym_null + sym_my_def + sym_wrapper + sym_sleep

        ehdr_size = 52
        off_shstr = ehdr_size
        off_str = off_shstr + len(shstrtab)
        off_sym = off_str + len(strtab)
        off_text = off_sym + len(symtab)
        off_shdrs = off_text + len(text)

        e_ident = b"\x7fELF\x01\x01\x01" + b"\x00" * 9
        ehdr = e_ident + struct.pack(
            "<HHIIIIIHHHHHH",
            2,  # e_type = ET_EXEC
            3,  # e_machine = EM_386
            1,  # e_version
            0x2000,  # e_entry
            0,  # e_phoff
            off_shdrs,  # e_shoff
            0,  # e_flags
            ehdr_size,  # e_ehsize
            0,  # e_phentsize
            0,  # e_phnum
            40,  # e_shentsize
            5,  # e_shnum
            1,  # e_shstrndx
        )

        # Elf32_Shdr: name, type, flags, addr, offset, size, link, info, align, entsize
        sh_null = struct.pack("<10I", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        sh_shstr = struct.pack(
            "<10I", 1, 3, 0, 0, off_shstr, len(shstrtab), 0, 0, 1, 0
        )
        sh_str = struct.pack(
            "<10I", 11, 3, 0, 0, off_str, len(strtab), 0, 0, 1, 0
        )
        sh_sym = struct.pack(
            "<10I", 19, 2, 0, 0, off_sym, len(symtab), 2, 0, 4, 16
        )
        sh_text = struct.pack(
            "<10I", 27, 1, 6, 0x2000, off_text, len(text), 0, 0, 4, 0
        )

        elf_bytes = (
            ehdr
            + shstrtab
            + strtab
            + symtab
            + bytes(text)
            + sh_null
            + sh_shstr
            + sh_str
            + sh_sym
            + sh_text
        )

        with tempfile.NamedTemporaryFile() as tmp:
            tmp.write(elf_bytes)
            tmp.flush()
            with mock.patch.object(
                check_sysworkq_blocking,
                "ELFFile",
                check_sysworkq_blocking._MiniELFFile,
            ):
                roots, violations = check_sysworkq_blocking.analyze_elf(
                    tmp.name, objdump_bin="nonexistent-objdump-binary"
                )
        self.assertEqual(len(roots), 1)
        self.assertEqual(roots[0].name, "my_def")
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].call_chain, ["my_def", "crec_usleep"])

    @mock.patch("check_sysworkq_blocking.analyze_elf")
    def test_cli_main_modes(self, analyze_mock):
        """Verify CLI return codes and JSON output formatting."""
        root = check_sysworkq_blocking.SysworkqRoot(
            kind="hook", name="bad_hook", source="HOOK_TICK", address=0x1000
        )
        violation = check_sysworkq_blocking.BlockingViolation(
            root_kind="hook",
            root_name="bad_hook",
            root_source="HOOK_TICK",
            sink_name="crec_usleep",
            call_chain=["bad_hook", "crec_usleep"],
        )
        analyze_mock.return_value = ([root], [violation])

        # Missing file returns exit code 2
        self.assertEqual(
            check_sysworkq_blocking.main(["/nonexistent/zephyr.elf"]), 2
        )

        with tempfile.NamedTemporaryFile() as tmp:
            # Violation found -> exit code 1
            rc = check_sysworkq_blocking.main(
                [
                    "--include-timeout-primitives",
                    "--allowlist",
                    "foo,bar",
                    tmp.name,
                ]
            )
            self.assertEqual(rc, 1)

            # --warn-only -> exit code 0
            stdout_buf = io.StringIO()
            with mock.patch("sys.stdout", stdout_buf):
                rc = check_sysworkq_blocking.main(
                    ["--warn-only", "--json", tmp.name]
                )
            self.assertEqual(rc, 0)
            parsed = json.loads(stdout_buf.getvalue())
            self.assertEqual(parsed[tmp.name]["roots_analyzed"], 1)
            self.assertEqual(len(parsed[tmp.name]["violations"]), 1)


if __name__ == "__main__":
    unittest.main()
