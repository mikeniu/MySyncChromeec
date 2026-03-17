# ET171 Flash Tool (`et171_flash`) Workflow

This document provides a step-by-step workflow for building and using the `et171_flash` tool from scratch. This tool is used for flashing Egis ET171 firmware and interacting with its bootloader.

## 1. Environment Setup & Dependencies

Ensure you are inside the ChromiumOS chroot environment (`cros_sdk`). The `et171_flash` tool requires `libusb`, `boringssl`, `libftdi`, and `libec`.

First, install the required dependencies for the target board (e.g., `amd64-generic`):
```sh
cros build-packages --board amd64-generic libusb boringssl libftdi libec
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
                              --key /path/to/key.bin \
                              --pid 0x5401
```

**2. Flashing Firmware (SHA256)**
If using SHA256, the `--key` parameter is not required (since SHA256 doesn't use a symmetric key for encryption).
```sh
./build/host/util/et171_flash --flashfw /path/to/fw.bin \
                              --algo SHA256 \
                              --pid 0x5401
```

**3. Saving Processed Commands to a File**
You can run the firmware mode and simultaneously dump the processed commands to a file for later use or debugging.
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
| `--flashfw` | `<fw.bin>` | **(Mode)** Flash firmware with real-time encryption. |
| `--flashcmd`| `<cmd.bin>` | **(Mode)** Flash a pre-processed raw command binary. |
| `--algo`    | `<type>` | Encryption algorithm: `SHA256` (default) or `AESGCM`. |
| `--key`     | `<key.bin>`| Path to the AES-GCM key file (required if algo is `AESGCM`). |
| `--offset`  | `<addr>` | Defines both the write address in Flash and the byte offset to skip in the firmware file. (Default: `0x00001000`, skips the first 4KB). |
| `--size`    | `<bytes>`| Size to flash in hex (Default: entire firmware file). |
| `--pid`     | `<hex>` | USB PID (Product ID) for App mode. Used to find the device before switching to bootloader. |
| `--dumpcmd` | `<file>` | Save processed commands to a file (only for `--flashfw` mode). |
| `--help, -h`| | Show the help message. |

## 5. Execution Workflow

When executed, the tool performs a precise sequence of operations to ensure safe and authenticated firmware flashing:

### Phase 1: Device Discovery & Mode Switching
The tool automatically handles devices whether they are in Application Mode or already in Bootloader Mode:
1. **App Mode Attempt**: Attempts to connect to the device in Application Mode using the provided `--pid` (Default App Profile: VID `0x18D1`, PID `0x5401`).
   - *If successful*: It fetches the firmware versions via the `EC_CMD_GET_VERSION` (`0x0002`) host command, prints the RO/RW versions, and sends the `EC_CMD_ENTER_BOOTLOADER` (`0x00E2`) host command to reboot the device into Bootloader Mode.
   - *If already in Bootloader*: It bypasses the App Mode commands and proceeds directly to step 2.
2. **Bootloader Connection**: Connects (or reconnects) to the device using the Bootloader Profile (VID `0x1C7A`, PID `0x1002`).
3. **Diagnostics Collection**: Fetches Bootloader information via `CommandId::kGetBootInfo` (`0x10`) and Flash diagnostics via `CommandId::kFlashInfo` (`0x11`).

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
6. **Command Dumping (Optional)**: If `--dumpcmd` is provided, all `kSecureWrapped` packets generated during this process are simultaneously saved to a file for later offline flashing.

#### Workflow B: `--flashcmd` (Raw Command Flashing)
This mode is used to replay a pre-processed dump file directly to the bootloader.
1. **Payload Parsing**: Reads the binary file containing pre-wrapped `CommandId::kSecureWrapped` (`0xF4`) commands. (Note: The parser also explicitly supports `CommandId::kSystemReset` (`0x12`) raw commands).
2. **Packet Segmentation**: Dynamically parses the secure headers to determine the exact size of each packet to prevent hardware buffer overflows.
3. **Direct USB Transmission**: Sends the raw command packets directly to the bootloader via USB bulk transfers.
4. **Status Verification**: Reads the response status header from the device after every packet to ensure the command was executed without errors (except for `kSystemReset` which triggers an immediate reboot and requires no response).
