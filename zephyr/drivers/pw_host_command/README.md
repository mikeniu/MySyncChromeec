# Pigweed RPC Host Command & Logging over HDLC UART: Technical Report & Findings

## 1. Executive Summary

This effort investigated and implemented a Proof-of-Concept (PoC) for executing **ChromeOS EC Host Commands** and streaming **EC logs** concurrently over a single physical UART interface using **Pigweed RPC** and **HDLC framing** (`pw_hdlc`).

Target platform:
- **Board/Program**: `moonstone` (Fatcat baseboard, ITE IT82002AW RISC-V EC)
- **Framework**: Zephyr RTOS (ChromeOS EC Zephyr repository)
- **Environment**: Zero dynamic memory allocation (`CONFIG_COMMON_LIBC_MALLOC=n`), strict flash partition limits (RO ≤ 385 KB, RW ≤ 393 KB).

---

## 2. Architecture & Design

```
+-------------------------------------------------------------------------+
|                              Host / Client                              |
|                                                                         |
|  +---------------------+                +----------------------------+  |
|  |   Interactive REPL  |                |   pw_hdlc.rpc.HdlcRpcClient|  |
|  |  (ec_pw_console.py) |                |                            |  |
|  +----------+----------+                +-------------+--------------+  |
|             |                                         |                 |
|             |                                         v                 |
|             |                              +----------------------+     |
|             |                              | SafeSerialReader     |     |
|             |                              +----------+-----------+     |
+-------------|-----------------------------------------|-----------------+
              |                                         | (PTY / UART)
              v                                         v
+-------------------------------------------------------------------------+
|                  ChromeOS EC Firmware (Zephyr on DUT)                   |
|                                                                         |
|               HDLC Channel 1 (Log)           HDLC Address 'R' (RPC)     |
|               (kDefaultLogAddress)            (kDefaultRpcAddress)      |
|                       |                                 |               |
|                       v                                 v               |
|           +-----------------------+         +-----------------------+   |
|           | pw_hc_printk_char_hook|         |  HostCommandService   |   |
|           |  printk / CPRINTF     |         |  (Nanopb RPC Service) |   |
|           +-----------+-----------+         +-----------+-----------+   |
|                       \                                 /               |
|                        \                               /                |
|                         v                             v                 |
|                      +-----------------------------------+              |
|                      |  ThreadSafeRpcChannelOutput       |              |
|                      |  (k_mutex protected UART TX)      |              |
|                      +-----------------+-----------------+              |
|                                        |                                |
|                                        v                                |
|                              Hardware UART Driver                       |
+-------------------------------------------------------------------------+
```

### Protocol Multiplexing
- **Address `'R'` (`0x52` / `82`)**: Pigweed RPC packets (`chromiumos.ec.HostCommandService.ExecuteCommand`).
- **Address `1` (`0x01` / `kDefaultLogAddress`)**: HDLC UI frames encapsulating system logs (`printk`, `CPRINTF`, `pw_log`).

---

## 3. Key Components Implemented

### A. Firmware RPC Service & HDLC Driver
- **Service Implementation** ([service.cc](file:///usr/local/google/home/asemjonovs/chromiumos/src/platform/ec/zephyr/drivers/pw_host_command/service.cc)):
  - Zero-heap Nanopb service invoking `host_command_process()`.
  - Statically allocated request/response buffers adhering to `CONFIG_PLATFORM_EC_HOSTCMD_MAX_PAYLOAD_SIZE` (512 bytes).
- **UART HDLC Server** ([uart_server.cc](file:///usr/local/google/home/asemjonovs/chromiumos/src/platform/ec/zephyr/drivers/pw_host_command/uart_server.cc)):
  - Dedicated Zephyr thread (`pw_hc_thread`, stack 2048 bytes) running the `pw::hdlc::Decoder` receive loop.
  - Intercepts Zephyr `printk` via `__printk_hook_install()` and packages console prints into atomic HDLC frames.
  - Mutual exclusion (`uart_tx_mutex`) ensuring interleaved log frames and RPC responses do not corrupt each other.
- **Embedded C++ Stubs**:
  - Added stub symbols (`sbrk`, `_sbrk`, `operator new/delete`, `__cxa_pure_virtual`, `__cxa_guard_*`) to prevent the toolchain from pulling in libstdc++ dynamic allocator routines into a no-heap binary.

### B. Python Client & Console
- **Interactive Client** ([ec_pw_console.py](file:///usr/local/google/home/asemjonovs/chromiumos/src/platform/ec/util/ec_pw_console.py)):
  - Embedded protobuf descriptor generation fallback for systems without pre-installed `host_command_pb2`.
  - Helper methods for `ec.hello()` (`EC_CMD_HELLO` 0x0001) and `ec.get_version()` (`EC_CMD_GET_VERSION` 0x0002).
  - Background log demultiplexing to print `[EC LOG] <message>` in real-time.

---

## 4. Key Discoveries & Root Cause Analysis

### 1. Zephyr Shell Echo Corrupting HDLC Streams (RO vs. RW)
- **Symptom**: Client received garbage frames like `\x1b[1D\x1b[J` with FCS CRC failures.
- **Root Cause**: `PLATFORM_EC_PW_HOST_COMMAND` was initially guarded with `depends on !CROS_EC_RO`. When the EC booted into RO post-flash, the standard Zephyr shell was active with terminal echo enabled, echoing incoming binary RPC bytes back onto TX with ANSI cursor escapes.
- **Resolution**: Enabled `PLATFORM_EC_PW_HOST_COMMAND` in both RO and RW configurations, ensuring `uart_shell_stop()` runs unconditionally.

### 2. PTY 0-Byte Non-Blocking Reads in `pyserial`
- **Symptom**: `DataReaderAndExecutor` thread crashed with `SerialException("device reports readiness to read but returned no data...")`.
- **Root Cause**: On Linux pseudo-terminals (`/dev/pts/*`), `select()` reports read-readiness when 0 bytes are immediately readable. Standard `pyserial` treats this as an unhandled error.
- **Resolution**: Implemented `SafeSerialReader(SerialReader)` which catches transient `SerialException` / `OSError` and introduces a small sleep before retrying.

### 3. Servod EC-3PO Line Interpreter Interference
- **Symptom**: Raw bytes read on `ec_uart_pty` contained ASCII timestamp insertions (`26-09-02 14:00:05.751 ...`), and HDLC flags (`0x7E`) were stripped.
- **Root Cause**: Servod attaches an EC-3PO line interpreter to the EC UART by default (`ec_ec3po_interp_connect:on`). EC-3PO intercepts the stream, buffers lines delimited by `\n`, and injects human-readable timestamps into binary HDLC payloads.
- **Resolution**: Disabled EC-3PO via servod control:
  ```bash
  dut-control -p <port> ec_ec3po_interp_connect:off
  ```
  This turns `raw_ec_uart_pty` into a bit-transparent binary pipe.

### 4. Zephyr UART RX Flow After `uart_shell_stop()`
- **Symptom**: RPC requests timed out with no response from the EC despite logs flowing out.
- **Root Cause**: Under `CONFIG_UART_INTERRUPT_DRIVEN`, `uart_shell_stop()` disables interrupts (`uart_irq_rx_disable`). When using `uart_getc()`, it relies on the interrupt ring buffer which stops receiving when interrupts are disabled.
- **Resolution**: Streamed all incoming bytes directly into `pw::hdlc::Decoder::Process()` using `uart_poll_in()` or re-enabling RX interrupts.

### 5. Flash Partition Constraints on IT8xxx2
- **Symptom**: Binman image packing failed with `ec-ro section size exceeded (397 KB > 385 KB)`.
- **Root Cause**: Nanopb code generation and Pigweed HDLC libraries increased flash footprint.
- **Resolution**: Trimmed auxiliary shell features in `project.conf` (`CONFIG_INPUT_SHELL=n`, `CONFIG_SHELL_HISTORY=n`, `CONFIG_SHELL_TAB_AUTOCOMPLETION=n`, `CONFIG_KERNEL_SHELL=n`), bringing RO down to 383 KB (36.5% of 1MB flash) and RW to 387 KB.

---

## 5. Verification & Test Results

1. **Unit & Integration Tests**:
   - `twister -T zephyr/test/dsp_comms/`: **100% Passed** (7/7 test suites, 110/110 test cases).
2. **Hardware HDLC Log Capture**:
   - Validated live on DUT (`moonstone` on labstation). Received and decoded EC logs over HDLC Address 1:
     ```text
     [EC LOG] [10702.079925 can't read any temp sensors!]
     [EC LOG] Node with matching prop not found
     [EC LOG] [10703.091583 can't read any temp sensors!]
     ```

---

## 6. Recommendations & Next Steps

1. **Servod Driver Integration**:
   - Add a servod driver setting / XML parameter (`hdlc_mode: on`) that automatically sets `ec_ec3po_interp_connect: off` when launching HDLC/Pigweed console sessions.
2. **Interrupt-Driven RX Ring Buffer**:
   - Implement an interrupt-driven RX callback in `uart_server.cc` rather than polling in a thread, minimizing EC CPU utilization during idle periods.
3. **Upstream Review & Modularity**:
   - Maintain the separation between core `HostCommandService` (transport-agnostic) and `uart_server` (transport-specific) to easily support other physical transports (e.g. SPI, I2C, USB stream).
