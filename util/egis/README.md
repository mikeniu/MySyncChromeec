# ET171 Flash Tool (`et171_flash`) Workflow

This document provides a step-by-step workflow for building and using the `et171_flash` tool from scratch. This tool is used for flashing Egis ET171 firmware and interacting with its bootloader.

## 1. Environment Setup & Dependencies

Ensure you are inside the ChromiumOS chroot environment (`cros_sdk`). The `et171_flash` tool requires `libusb` and `boringssl`.

First, install the required dependencies for the target board (e.g., `amd64-generic`):
```sh
cros build-packages --board amd64-generic libusb boringssl
USE="static-libs -udev" emerge-amd64-generic libusb
```

## 2. Building the Tool

The tool is part of the `ec-devutils` package. Use `cros_workon` to start working on it, then build it via `cros_workon_make`:

```sh
# Mark the package as being actively worked on
cros_workon --board=amd64-generic start ec-devutils

# Navigate to the EC source directory
cd ~/chromiumos/src/platform/ec

# Clean the previous build and build the utilities
rm -rf build && cros_workon_make --board=amd64-generic ec-devutils
```

Once successfully built, the executable will typically be located in the build output directory under `build/host/util/et171_flash`.

## 3. Tool Usage & Modes

The `et171_flash` tool supports two mutually exclusive modes:
1. **`--flashfw`**: Flash firmware with real-time encryption.
2. **`--flashcmd`**: Flash a pre-processed raw command binary.

### Example Commands

**1. Flashing Firmware (AES-GCM)**
Flashing an image with AES-GCM encryption requires providing the firmware binary and the encryption key.
```sh
./build/host/util/et171_flash --flashfw /path/to/fw.bin \
                              --algo AESGCM \
                              --key /path/to/key.bin
```

**2. Flashing Firmware (SHA256)**
If using SHA256, the `--key` parameter is not required (since SHA256 doesn't use a symmetric key for encryption).
```sh
./build/host/util/et171_flash --flashfw /path/to/fw.bin \
                              --algo SHA256
```

**3. Saving Processed Commands to a File (Offline Dry-run)**
If you specify `--dumpcmd`, the tool runs in an offline dry-run mode. It performs the complete encryption and packaging workflow and saves the processed commands to a file without requiring a physical device or performing actual flashing.
```sh
./build/host/util/et171_flash --flashfw /path/to/fw.bin \
                              --algo AESGCM \
                              --key /path/to/key.bin \
                              --dumpcmd /path/to/cmd_dump.bin
```

**4. Flashing a Pre-processed Command Binary**
If you already have a pre-processed raw command binary (e.g., generated using `--dumpcmd`), you can flash it directly without needing the key again.
```sh
./build/host/util/et171_flash --flashcmd /path/to/cmd_dump.bin
```

**5. Viewing Help**
```sh
./build/host/util/et171_flash --help
```

## 4. Command Line Options

| Option | Argument | Description |
| :--- | :--- | :--- |
| `--flashfw, -f` | `<fw.bin>` | **(Mode)** Flash firmware with real-time encryption. |
| `--flashcmd, -c`| `<cmd.bin>` | **(Mode)** Flash a pre-processed raw command binary. |
| `--algo, -a`    | `<type>` | Encryption algorithm: `SHA256` (default) or `AESGCM`. |
| `--key, -k`     | `<key.bin>`| Path to the AES-GCM key file (required if algo is `AESGCM`). |
| `--offset, -o`  | `<addr>` | Defines both the write address in Flash and the byte offset to skip in the firmware file. (Default: `0x00001000`, skips the first 4KB). |
| `--size, -s`    | `<bytes>`| Size to flash in hex (Default: entire firmware file). |
| `--dumpcmd, -d` | `<file>` | Save processed commands to a file (for `--flashfw` mode). Runs in offline dry-run mode without requiring a physical device. |
| `--help, -h`| | Show the help message. |

## 5. Execution Workflow

When executed, the tool performs a precise sequence of operations to ensure safe and authenticated firmware flashing:

### Phase 1: Device Discovery & Connection
The tool connects directly to the device in Bootloader Mode:
1. **Bootloader Connection**: Connects to the device using the Bootloader Profile (VID `0x1C7A`, PID `0x1002`). It will retry connecting for up to 5 seconds (every 200ms) to allow the device time to become available.
2. **Diagnostics Collection**: Fetches Bootloader information via `CommandId::kGetBootInfo` (`0x10`) and Flash diagnostics via `CommandId::kFlashInfo` (`0x11`).

### Phase 2: Flashing Execution
Depending on the selected mode, the tool executes one of the following workflows:

#### Workflow A: `--flashfw` (Real-time Encryption Flashing)
This is the standard mode for flashing raw firmware binaries.
1. **Security Initialization**: Loads the AES key (if AES-GCM is selected) into the cryptographic context, and immediately scrubs the plaintext key from memory (`crypto::Cleanse`) for security.
2. **Device Preparation**: Disables the hardware watchdog and flash write-protection (WP) using `CommandId::kMemWrite` (`0x21`) payload commands:
   - **Watchdog**: Disabled by writing to the **PWM controller registers** (e.g., `kRegPwmCtrl`, `kRegPwmReload`), as the hardware timer is managed by PWM channels.
   - **Write Protection (WP)**: Disabled by writing to the **SPI controller registers**, which indirectly instructs the controller to issue native SPI commands like `Write Enable` (`0x06`) and `Write Status Register` (`0x01`) to clear protection bits.
3. **Flash Erase**: Calculates the exact sectors needed based on the firmware size and offset, and sends the `CommandId::kFlashErase` (`0x22`) command.
4. **Chunked Writing**: Divides the firmware payload into manageable chunks (max 32KB per write) and prepares `CommandId::kMemWrite` (`0x21`) payload commands.
5. **Dynamic Wrapping**: Every bootrom command (such as `kMemWrite` or `kFlashErase`) is dynamically wrapped inside a `CommandId::kSecureWrapped` (`0xF4`) packet with a secure header (SHA256 hash or AES-GCM encryption + MAC tag) before being sent over USB. For AES-GCM, the Chip ID (`0x00000001`) is injected as Additional Authenticated Data (AAD) to bind the payload to this specific hardware.
6. **Command Dumping (Optional)**: If `--dumpcmd` is provided, the tool runs in offline dry-run mode. It mocks device responses (such as register reads and flash status checks) so that all `kSecureWrapped` packets generated during this process are successfully packed and saved to a file for later offline flashing.

#### Workflow B: `--flashcmd` (Raw Command Flashing)
This mode is used to replay a pre-processed dump file directly to the bootloader.
1. **Payload Parsing**: Reads the binary file containing pre-wrapped `CommandId::kSecureWrapped` (`0xF4`) commands. (Note: The parser also explicitly supports `CommandId::kSystemReset` (`0x12`) raw commands).
2. **Packet Segmentation**: Dynamically parses the secure headers to determine the exact size of each packet to prevent hardware buffer overflows.
3. **Direct USB Transmission**: Sends the raw command packets directly to the bootloader via USB bulk transfers.
4. **Status Verification**: Reads the response status header from the device after every packet to ensure the command was executed without errors (except for `kSystemReset` which triggers an immediate reboot and requires no response).
