# EC Firmware Docker for Dagwood Testing

This directory contains the Docker and scripting configuration to set up an
isolated, automated build and test environment for building ChromiumOS EC
firmware and running tests on the Dagwood test fixture.

The docker build is setup to clone all repositories needed to build EC
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
To spin up the container, map your persistent local workspace, map the
persistent SDK toolchain cache, and drop into a bash shell with all dependencies
ready:

```bash
docker run -it --rm \
  -v $(pwd)/workspace:/workspace \
  -v $(pwd)/workspace/.cache/coreboot-sdk:/root/.cache/coreboot-sdk \
  ec-builder
```

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
specify the `--checkout /workspace` option when running `zmake commands.

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
To run tests against a Dagwoord board and EC Add-in-card (AIC) connected to the
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
