# EC Firmware Docker for Dagwood Testing

This directory contains the Docker and scripting configuration to set up an
isolated, automated build and test environment for building ChromiumOS EC
firmware and running tests on the Dagwood test fixture.

The docker build is set up to clone all repositories needed to build EC
firmware images and run the twister based tests.

When entering the container, the entrypoint.sh script pulls the latest
changes from all cloned repositories, so that EC firmware repos are
always up to date.

---

## Directory Structure Overview

The workspace directory structure matches standard expectations of ChromiumOS
`zmake` module lookups, using a `src/` nested repository layout:
*   `Dockerfile`: Builds the optimized Ubuntu-based environment with compiler
    pre-requisites.
*   `entrypoint.sh`: Runs on container startup to fetch/update checkouts,
    initialize python virtual environments, auto-install `.vpython3`
    dependencies, and export Coreboot SDK environment variables dynamically.
*   `workspace/`: Bind mount source for the container. Tracked, but empty, so
    that a fresh checkout owns it; everything below it is ignored.
*   `workspace/src/platform/ec/`: The Chromium OS EC firmware source.
*   `workspace/src/platform/dagwood/`: The Dagwood test/verification tool.
*   `workspace/src/third_party/zephyrproject/`: Zephyr RTOS project
    dependencies.
*   `workspace/src/third_party/pigweed/`: Google's Pigweed libraries.
*   `workspace/src/third_party/u-boot/`: U-Boot tools repository (contains
    `binman` python signing packages).
*   `workspace/src/third_party/chromiumos-overlay/`: A sparse-checkout
    containing eclass files needed to resolve SDK dependencies.
*   `workspace/.cache/coreboot-sdk/`: Persistent cache folder on the host
    storing the downloaded cross-compilation toolchains.
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
To spin up the container, map your persistent local workspace, and drop into a
bash shell with all dependencies ready:

```bash
docker run -it --rm \
  -v $(pwd)/workspace:/workspace \
  ec-builder
```

Or using the helper script:
```bash
./run_docker.sh
```

> **The container runs as the owner of `workspace/`.** That directory is part
> of the checkout, so it belongs to whoever cloned the tree, and build
> artifacts land on the host owned by them. Do not delete it: Docker recreates
> a missing bind mount source as `root:root`, leaving the container no way to
> tell who started it, so it runs everything as root instead. To recover, run
> `sudo chown -R "$(id -u):$(id -g)" workspace`.

> **Fast Startup**: Pass `--fast` (or set `SKIP_UPDATE=1`) to bypass remote
> repository git checks for sub-second container launch:
> ```bash
> ./run_docker.sh --fast
> ```

### 3. Hardware Access (Flashing and Device Testing)
The plain `docker run` above is enough for building. Flashing and device
testing additionally need the host's USB devices and serial ports, which
`run_docker.sh` configures automatically. To do the same from a raw
`docker run`, add:

```bash
docker run -it --rm \
  --device-cgroup-rule="c *:* rmw" \
  -v $(pwd)/workspace:/workspace \
  -v /dev:/dev \
  ec-builder
```

Bind mounting `/dev` keeps devices visible across hotplug and
re-enumeration, so a board that resets into its bootloader and returns
under a different `ttyACM` number stays usable. The cgroup rule is
required because Docker denies `open()` on devices outside a small
default allowlist; limiting it to character devices leaves block devices
(disks) inaccessible. `--privileged` is not needed.

---

## Using the Tools (Inside the Container)

Once you have entered the container, the virtual environment is automatically
activated and environment variables are exported globally.

### Verify `zmake` is working
```bash
zmake --help
```

### Run `zmake` Builds
To build a specific board configuration (e.g., `skyrim`):
```bash
zmake --checkout /workspace build skyrim
```

Because zmake is not running inside a full cros_sdk checkout, you must always
specify the `--checkout /workspace` option when running `zmake` commands.

The final firmware image will be populated on your host filesystem at
`workspace/src/platform/ec/build/zephyr/skyrim/output/ec.bin`.

### Run Twister Tests

#### 1. Host-based Emulation Testing
To run host-based emulation tests using Zephyr's Twister inside the container:

1. Navigate to the EC platform directory:
   ```bash
   cd /workspace/src/platform/ec
   ```
2. Run the tests using `python3 ./twister` (prefixing with `python3` is required
   to bypass the `vpython3` shebang):
   ```bash
   python3 ./twister -ivc -s hibernate_z5.default
   ```

#### 2. Real Device Testing on Dagwood (using Helper Script)
To run tests against a Dagwood board and EC Add-in-card (AIC) connected to the
host, you can use the `run_dagwood_tests.py` helper script. It automatically
handles device forwarding and configures twister with the required parameters
(toolchain, flash command, etc.).

Test results are available on your host filesystem at
`workspace/src/platform/ec/twister-out/`.

##### Realtek (RTS5912) Example
For Realtek boards, the container automatically builds and pre-installs the
`rtkupdate` utility and the `rts5915_flash_upload.bin` monitor binary on
startup.

To run all EC-AIC tests on `realtek/rts5912`:
```bash
./run_dagwood_tests.py -p realtek/rts5912
```

##### Nuvoton (NPCX9) Example
For Nuvoton boards, the container automatically builds and pre-installs the
`uartupdatetool` utility and the `npcx_monitor.bin` monitor binary on
startup.

To run all EC-AIC tests on `npcx9/npcx9m7f`:
```bash
./run_dagwood_tests.py -p npcx9/npcx9m7f
```

##### Customizing the Run
You can override the defaults using the script options:

*   **Specify one or more test directories**:
    ```bash
    ./run_dagwood_tests.py -p realtek/rts5912 -T zephyr/test/ec-aic -T zephyr/test/another-test
    ```
*   **Run one or more specific test scenarios**:
    ```bash
    ./run_dagwood_tests.py -p realtek/rts5912 -s aic.i2c -s another.scenario
    ```
*   **Specify a different serial port**:
    ```bash
    ./run_dagwood_tests.py -p realtek/rts5912 -d /dev/ttyACM0
    ```

For help on options:
```bash
./run_dagwood_tests.py -h
```

### Direct Command Execution (Without Entering Container)
You can trigger builds or run tests directly from your host machine:

**For builds:**
```bash
docker run --rm \
  -v $(pwd)/workspace:/workspace \
  ec-builder zmake --checkout /workspace build skyrim
```

**For Twister tests:**
```bash
docker run --rm \
  -v $(pwd)/workspace:/workspace \
  ec-builder bash -c "cd /workspace/src/platform/ec && python3 ./twister -ivc -s hibernate_z5.default"
```

### Building and Flashing Dagwood Firmware (OpenOCD & ST-Link)
The container includes `openocd` and the `hal_stm32` Zephyr module to build
and flash the Dagwood fixture MCU firmware directly using an attached ST-Link
debugger:

1. **Inside the container**:
   ```bash
   cd /workspace/src/platform/dagwood
   # Build and flash via OpenOCD & ST-Link:
   ./build_from_chroot.py -f
   # Or flash existing build without rebuilding:
   ./build_from_chroot.py --flash-only
   ```

2. **Direct from host via `run_docker.sh`**:
   ```bash
   ./run_docker.sh bash -c "cd /workspace/src/platform/dagwood && ./build_from_chroot.py -f"
   ```
