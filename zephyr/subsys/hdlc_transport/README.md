# HDLC Transport Subsystem

The High-Level Data Link Control (HDLC) transport subsystem introduces
packet-based framing over the Embedded Controller (EC) serial interface (UART).
By leveraging Pigweed's [pw_hdlc](https://pigweed.dev/pw_hdlc/) protocol, this
subsystem multiplexes multiple logical streams—tokenized/standard logging, an
interactive Zephyr shell, and Pigweed Remote Procedure Call (RPC)—over a single
physical UART connection.

This document details the architectural design of the EC-side HDLC transport,
the host-side console and detokenizer infrastructure
([`ec_console.py`](/util/ec_console.py)), and an empirical footprint analysis
comparing standard and HDLC-enabled builds on the **Moonstone** board.

[TOC]

## Background and Motivation

Traditionally, the ChromeOS EC UART has carried unstructured raw ASCII text
interspersed with base64-encoded tokenized logs. This model presents several
operational challenges:

1. **Log/Shell Interleaving**: Asynchronous log messages (e.g., from interrupts
   or background threads) corrupt interactive typing in the console, breaking
   prompt formatting and command entry.
2. **Protocol Collision**: Introducing binary protocols (such as Pigweed RPC or
   binary trace data) alongside human-readable shells requires separate physical
   UART ports or invasive out-of-band switching mechanisms.
3. **Log Corruption**: If a log string is emitted mid-escape-sequence or during
   shell output, host-side log parsers (e.g., detokenizers) can lose sync or drop
   frames.

By encapsulating all UART communications in lightweight HDLC frames, logical
channels are cleanly isolated at the link layer.

```text
+-------------------------------------------------------------------+
|                          Zephyr EC (DUT)                          |
|                                                                   |
|   +---------------------+   Channel 1 (Log)                       |
|   | Logging / Tokenizer | ------------------+                     |
|   +---------------------+                   |                     |
|   +---------------------+   Channel 2 (Shell)                     |
|   | Zephyr Shell        | ------------------+                     |
|   +---------------------+                   |                     |
|   +---------------------+   Channel 0x52 (RPC)                    |
|   | Pigweed RPC         | ------------------+                     |
|   +---------------------+                   |                     |
|                                             v                     |
|                                   +-------------------+           |
|                                   |  HDLC Transport   |           |
|                                   |     Subsystem     |           |
|                                   +-------------------+           |
|                                             |                     |
|                                             v                     |
|                                   +-------------------+           |
|                                   |    UART Driver    |           |
|                                   +-------------------+           |
+---------------------------------------------|---------------------+
                                              | Physical UART
                                              | (Servo / CCD)
                                              v
+-------------------------------------------------------------------+
|                            Host Machine                           |
|                                                                   |
|                       +-------------------+                       |
|                       |   ec_console.py   |                       |
|                       |  (Demux / Frame)  |                       |
|                       +-------------------+                       |
|                         /        |        \                       |
|           Channel 1    /         | Channel 2\  Channel 0x52       |
|           + Token DB  /          |           \                    |
|                      v           v            v                   |
|             +-------------+ +-----------+ +---------------+       |
|             | Detokenized | | Interactive| |  Pigweed RPC |       |
|             |  Log Panel  | | Shell TUI | |    Client     |       |
|             +-------------+ +-----------+ +---------------+       |
+-------------------------------------------------------------------+
```

---

## Subsystem Architecture

The HDLC transport implementation is divided into the firmware subsystem on the
EC and the host-side console utility.

### Firmware Subsystem (`zephyr/subsys/hdlc_transport/`)

The firmware components reside in this directory:

* **Transport Core ([`hdlc_transport.cc`](src/hdlc_transport.cc))**:
  * **TX Path**: `hdlc_send_frame()` formats and escapes payloads into
    unnumbered information (UI) frames with 32-bit CRC (`pw_checksum`). It
    serializes writes across threads using a mutex, with an automatic bypass
    during kernel panics or ISR contexts to ensure crash dumps are delivered.
  * **RX Path**: Interrupt-driven UART reads (`uart_fifo_read`) push incoming
    bytes into a lock-free ring buffer (`CONFIG_HDLC_TRANSPORT_RX_BUF_SIZE`). A
    dedicated worker thread running `hdlc_rx_thread_fn` decodes frames using
    `pw::hdlc::Decoder` and dispatches payloads based on address.
  * **Routing Table**: Supports dynamic registration via
    `hdlc_register_handler()` for up to 8 distinct logical channel addresses.

* **HDLC Shell Backend ([`shell_hdlc.c`](src/shell_hdlc.c))**:
  * Implements the Zephyr `shell_transport_api`.
  * Directs input and output to `HDLC_ADDR_SHELL` (`0x02`), decoupling the
    shell from the raw UART hardware.

* **Dedicated Log Backend ([`log_backend_hdlc.c`](src/log_backend_hdlc.c))**:
  * Implements a Zephyr logging backend (`log_backend_api`), encapsulating log
    messages directly to `HDLC_ADDR_LOG` (`0x01`).

* **Tokenized Logging Integration ([`pw_log_zephyr_tokenized.cc`](/zephyr/shim/src/pw_log_zephyr_tokenized.cc))**:
  * Emits tokenized base64 log messages over `HDLC_ADDR_LOG`, preserving ISR
    context flags (`[ISR]`) without corrupting shell command line buffers.

### Standard Channel Allocations

| Channel Address | Identifier | Description |
|---|---|---|
| `0x01` (`1`) | `HDLC_ADDR_LOG` | EC log channel (tokenized binary/base64 or plaintext logs) |
| `0x02` (`2`) | `HDLC_ADDR_SHELL` | Bidirectional interactive Zephyr shell session |
| `0x52` (`'R'`) | `HDLC_ADDR_RPC` | Pigweed RPC communication channel |

### Host-Side Utility (`util/ec_console.py`)

The host utility ([`util/ec_console.py`](/util/ec_console.py)) connects to the
physical or Servo UART (e.g., via `dut-control raw_ec_uart_pty`), providing:

* **Dual View Terminal UI (TUI)**: Simultaneous interactive shell panel and
  real-time scrolling log panel.
* **Online Detokenization**: Automatically decodes tokenized log strings using
  the compiled token database (`database.bin` or `tokens.bin`).
* **Virtual PTY Mode**: Exposes a standard UNIX pseudo-terminal for compatibility
  with legacy test scripts and tools (`ectool`, automated test frameworks) while
  streaming demultiplexed logs to a separate file.
* **Scrollback & Mouse Support**: Provides scrollback buffers, page-up/page-down
  navigation, and mouse wheel support.

---

## Image Size & Memory Analysis (Moonstone)

Footprint metrics were evaluated on **Moonstone** comparing the baseline build
(`CONFIG_SHELL_BACKEND_SERIAL=y`) against the HDLC configuration
(`CONFIG_HDLC_TRANSPORT=y`, `CONFIG_SHELL_BACKEND_HDLC=y`).

### Flash Binary Footprint (`zephyr.bin`)

| Binary Image | Standard (`moonstone`) | With HDLC (`moonstone.hdlc`) | Delta (Bytes) | % Change |
|---|---|---|---|---|
| **RO Flash Binary (`zephyr.bin`)** | 337,972 B | 339,412 B | **+1,440 B** | **+0.43%** |
| **RW Flash Binary (`zephyr.bin`)** | 342,120 B | 339,464 B | **-2,656 B** | **-0.78%** |

*** note
**Why did the RW binary size decrease?**

Replacing `CONFIG_SHELL_BACKEND_SERIAL` with `CONFIG_SHELL_BACKEND_HDLC` removed
`shell_uart.c` from the Instruction Local Memory (ILM/ITCM) staging region,
reducing its size by 1,052 bytes. In the standard RW build, the ILM staging
region had exceeded a 4 KB boundary (`ALIGN(0x1000)`), introducing **3,197 bytes
of zero-padding**. Removing `shell_uart` brought the section back under the
boundary, eliminating 4,096 bytes of alignment fill in the output binary.
***

### Code & Constant Breakdown (Flash Sections)

Excluding linker fill/alignment padding, the net difference in executable code
and read-only data is **+3,124 bytes**:

| Section | Baseline Size | HDLC Size | Delta | Description |
|---|---|---|---|---|
| `.text` | 270,104 B | 272,004 B | **+1,900 B** | Executable instructions |
| `.rodata` | 46,512 B | 47,704 B | **+1,192 B** | Constant data (includes 1,088 B CRC32 table) |
| `.data` (LMA) | 11,576 B | 11,600 B | **+32 B** | Initialized data copied from flash to RAM |
| **Net Code & Data Impact** | **328,192 B** | **331,316 B** | **+3,124 B** | **Total persistent flash requirement** |

### Component Delta Analysis

#### Additions (+4,182 B Flash)

* **`hdlc_transport.cc.obj`**: **+1,318 B** text (frame encoding, dispatch loop,
  ring buffer management)
* **`pw_checksum/crc32.cc.obj`**: **+1,088 B** rodata, **+162 B** text (fast
  lookup table & calculation)
* **`pw_hdlc/encoder.cc.obj`**: **+592 B** text (HDLC UI frame builder & byte
  stuffer)
* **`pw_hdlc/decoder.cc.obj`**: **+556 B** text (streaming state machine &
  frame validator)
* **`shell_hdlc.c.obj`**: **+230 B** text (Zephyr shell transport interface
  glue)
* **`pw_log_zephyr_tokenized.cc.obj`**: **+72 B** text (HDLC routing branch)
* **`initlevel` / `init_array`**: **+12 B**

#### Removals (-1,058 B Flash)

* **`shell_uart.c.obj`**: **-1,008 B** text, **-44 B** rodata, **-16 B** data
  (eliminated raw serial shell backend)
* **Heap / Console / Utilities**: **-164 B** text & rodata

### RAM Impact

| Section | Baseline Size | HDLC Size | Delta | Description |
|---|---|---|---|---|
| `.bss` | 25,172 B | 25,612 B | **+408 B** | Uninitialized static RAM |
| `ilm_ram` | 54,147 B | 52,959 B | **-1,188 B** | Fast SRAM allocated for code execution |
| `.noinit` | 20,736 B | 23,296 B | **+2,560 B** | RX ring buffers & thread stack allocations |
| **Net RAM Delta** | — | — | **+1,780 B** | **Combined runtime RAM delta** |

* **`.bss` (+408 B)**: Added +1,444 B in `hdlc_transport` and +1,252 B in
  `shell_hdlc`, balanced by the removal of 2,288 B of raw UART shell queues and
  ring buffers.
* **`ilm_ram` (-1,188 B)**: Instruction Local Memory allocation decreased
  because the HDLC shell backend is not mapped to ILM, leaving more fast SRAM
  available.
* **`.noinit` (+2,560 B)**: Reflects the 512-byte UART RX ring buffer, decoder
  buffer, and stack alignment for the dedicated `hdlc_rx_thread` (2,048-byte
  stack).

---

## Configuration & Usage Guide

### Board Kconfig Options

To enable HDLC transport on a project (e.g. in
[`project.conf`](/zephyr/program/fatcat/moonstone/project.conf)):

```ini
# Enable HDLC Transport subsystem and Pigweed dependencies
CONFIG_HDLC_TRANSPORT=y

# Require interrupt-driven UART driver for asynchronous reception
CONFIG_UART_INTERRUPT_DRIVEN=y

# Route Zephyr shell through HDLC channel 2
CONFIG_SHELL_BACKEND_SERIAL=n
CONFIG_SHELL_BACKEND_HDLC=y
```

### Launching the Host Console

Run [`util/ec_console.py`](/util/ec_console.py) pointing to the DUT console PTY:

```bash
# Automatic PTY lookup via dut-control and default token database:
./util/ec_console.py

# Explicit PTY device and token database:
./util/ec_console.py -d /dev/pts/12 -t build/zephyr/moonstone/output/database.bin

# Expose a virtual PTY for automated testing tools:
./util/ec_console.py --pty /tmp/ec_shell_pty --log-file /tmp/ec_stream.log
```

---

## Conclusion

The HDLC transport subsystem provides a robust, extensible link layer for the
Zephyr EC. For a net persistent flash cost of approximately **3.1 KB** (and a
**-2.6 KB** net reduction in the final RW binary due to alignment boundary
optimization), it achieves complete channel isolation for logging, shell
interaction, and future RPC integrations without requiring multiple physical
UART ports.
