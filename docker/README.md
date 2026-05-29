# EC Firmware Docker Development Environment

This directory contains the Docker and scripting configuration to set up an isolated, automated build and test environment for building Chromium OS EC Firmware.

---

## Directory Structure Overview

The workspace directory structure matches standard expectations of ChromiumOS `zmake` module lookups, using a `src/` nested repository layout:
*   `Dockerfile`: Builds the lightweight, optimized Ubuntu-based environment (under 650MB) with compiler pre-requisites.
*   `entrypoint.sh`: Runs on container startup to fetch/update checkouts, initialize python virtual environments, auto-install `.vpython3` dependencies, and export Coreboot SDK environment variables dynamically.
*   `workspace/src/platform/ec/`: The Chromium OS EC firmware source.
*   `workspace/src/platform/dagwood/`: The Dagwood test/verification tool.
*   `workspace/src/third_party/zephyrproject/`: Zephyr RTOS project dependencies.
*   `workspace/src/third_party/pigweed/`: Google's Pigweed libraries.
*   `workspace/src/third_party/u-boot/`: U-Boot tools repository (contains `binman` python signing packages).
*   `workspace/src/third_party/chromiumos-overlay/`: A sparse-checkout containing eclass files needed to resolve SDK dependencies.
*   `workspace/.cache/coreboot-sdk/`: Persistent cache folder on the host storing the downloaded cross-compilation toolchains.
*   `workspace/.venv/`: The Python virtual environment inside the container.

---

## Getting Started

### Prerequisites
Ensure you have **Docker** installed and running on your workstation.

### 1. Build the Docker Image
Run the following command inside this directory to build the image:

```bash
docker build -t ec-builder .
```

### 2. Run the Container (Interactive Development)
To spin up the container, map your persistent local workspace, map the persistent SDK toolchain cache, and drop into a bash shell with all dependencies ready:

```bash
docker run -it --rm \
  -v $(pwd)/workspace:/workspace \
  -v $(pwd)/workspace/.cache/coreboot-sdk:/root/.cache/coreboot-sdk \
  ec-builder
```

> [!NOTE]
> The first time you enter the container, it will dynamically clone all external repositories (EC, Dagwood, Zephyr, Pigweed, U-Boot, and sparse Overlay) and download/extract the architectural Coreboot SDK cross-compilers into your host's `.cache` folder.
>
> Subsequent entries will skip downloads and start instantly since the checkouts and compilers are persisted on your host machine under the mapped volumes.

---

## Using the Tools (Inside the Container)

Once you have entered the container, the virtual environment is automatically activated and environment variables are exported globally.

### Verify `zmake` is working
```bash
zmake --help
```

### Run `zmake` Builds
To build a specific board configuration (e.g., `skyrim`):
```bash
zmake --checkout /workspace build skyrim
```

The finalized signed firmware image will be populated on your host filesystem at `workspace/src/platform/ec/build/zephyr/skyrim/output/ec.bin`.

### Run Twister Tests

#### 1. Host-based Emulation Testing
To run host-based emulation tests using Zephyr's Twister inside the container:

1. Navigate to the EC platform directory:
   ```bash
   cd /workspace/src/platform/ec
   ```
2. Run the tests using `python3 ./twister` (prefixing with `python3` is required to bypass the `vpython3` shebang):
   ```bash
   python3 ./twister -ivc -s hibernate_z5.default
   ```

#### 2. Real Device Testing (Hardware)
To run tests against a real device connected to the host, you must use the `run_docker.sh` helper script, which automatically forwards USB and active serial devices (`/dev/ttyACM*`) to the container.

##### Realtek (RTS5912) Example
For Realtek boards, flashing requires the `rtkupdate` utility and the `rts5915_flash_upload.bin` monitor binary. Follow these steps to build them and run the tests:

1.  **Launch the container interactively** using the helper script:
    ```bash
    ./run_docker.sh
    ```
2.  **Build the monitor binary** `rtk_flame`:
    ```bash
    zmake --checkout /workspace build rtk_flame
    ```
3.  **Copy the monitor binary** to the expected system path:
    ```bash
    mkdir -p /usr/share/ec-devutils
    cp /workspace/src/platform/ec/build/zephyr/rtk_flame/build-singleimage/rts5915_flash_upload.bin /usr/share/ec-devutils/
    ```
4.  **Manually compile the `rtkupdate` host utility** (avoiding full hosttools build dependencies):
    ```bash
    mkdir -p /workspace/src/platform/ec/build/host/util
    g++ -O2 -Wall -o /workspace/src/platform/ec/build/host/util/rtkupdate \
      /workspace/src/platform/ec/util/rtkupdate.cc \
      /workspace/src/platform/ec/util/uart_utils.cc \
      -I/workspace/src/platform/ec/util
    ```
5.  **Run Twister** specifying the device serial, flash command (with `--no-preserve` to skip FMAP verification if `dump_fmap` is missing), and coreboot-sdk toolchain:
    ```bash
    cd /workspace/src/platform/ec
    python3 ./twister -ivc -T zephyr/test/ec-aic \
      --toolchain=coreboot-sdk -p realtek/rts5912 \
      --device-testing --device-serial /dev/ttyACM1 \
      --flash-command ../dagwood/flash.py,--no-preserve \
      --device-flash-timeout 60
    ```

### Direct Command Execution (Without Entering Container)
You can trigger builds or run tests directly from your host machine:

**For builds:**
```bash
docker run --rm \
  -v $(pwd)/workspace:/workspace \
  -v $(pwd)/workspace/.cache/coreboot-sdk:/root/.cache/coreboot-sdk \
  ec-builder zmake --checkout /workspace build skyrim
```

**For Twister tests:**
```bash
docker run --rm \
  -v $(pwd)/workspace:/workspace \
  -v $(pwd)/workspace/.cache/coreboot-sdk:/root/.cache/coreboot-sdk \
  ec-builder bash -c "cd /workspace/src/platform/ec && python3 ./twister -ivc -s hibernate_z5.default"
```

