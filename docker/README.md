# EC Firmware Docker for Dagwood Testing

This directory contains the Docker and scripting configuration to set up an
isolated, automated build and test environment for building ChromiumOS EC
firmware and running tests on the Dagwood test fixture.

The docker build is set up to clone all repositories needed to build EC
firmware images and run the twister based tests.

When entering the container, `entrypoint.sh` populates `/workspace` from the
repositories cached in the image on first run and leaves existing checkouts
untouched on subsequent entries. Pass `--update` to pull the latest upstream
changes.

---

## Directory Structure Overview

The workspace directory structure matches standard expectations of ChromiumOS
`zmake` module lookups, using a `src/` nested repository layout:
*   `Dockerfile`: Builds the optimized Ubuntu-based environment with compiler
    pre-requisites.
*   `entrypoint.sh`: Runs on container startup to populate/update checkouts,
    initialize python virtual environments, and auto-install `.vpython3`
    dependencies.
*   `compose.yaml`: How the container runs: its workspace mount, hardware
    access, and named task services.
*   `run_docker.sh`: Starts the container with `docker compose run --rm`.
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
docker compose build
```

### 2. Run the Container (Interactive Development)
To spin up the container, map your persistent local workspace, and drop into a
bash shell with all dependencies ready:

```bash
docker compose run --rm shell
```

Or using the helper script:
```bash
./run_docker.sh
```

`run_docker.sh` runs `docker compose run --rm shell` with the configuration in
`compose.yaml`, and passes its arguments on as the command to run.

> **The container runs as the owner of `workspace/`.** That directory is part
> of the checkout, so it belongs to whoever cloned the tree, and build
> artifacts land on the host owned by them. Do not delete it: Docker recreates
> a missing bind mount source as `root:root`, leaving the container no way to
> tell who started it, so it runs everything as root instead. To recover, run
> `sudo chown -R "$(id -u):$(id -g)" workspace`.

> **Only `--rm` containers are supported.** `run_docker.sh` always passes
> `--rm`. Each time a container starts, the entrypoint creates a user for the
> owner of `workspace/`, so a stopped container cannot be started again: it
> exits with `useradd: user 'hostuser' already exists`. For the same reason,
> do not start the container with `docker compose up` or `docker compose
> start`.

> **Updating Repositories**: Container startup skips remote git checks by
> default so existing checkouts launch immediately and local state is left
> untouched. Pass `--update` to pull the latest upstream changes across all
> workspace repositories:
> ```bash
> ./run_docker.sh --update
> ```

### 3. Hardware Access (Flashing and Device Testing)
The container, as configured by `compose.yaml`, can access the host's USB
devices and serial ports, including devices that appear or re-enumerate after
the container starts. A board that resets into its bootloader and returns
under a different `ttyACM` number remains usable.

---

## Using the Tools (Inside the Container)

Once you have entered the container, the virtual environment is automatically
activated and environment variables are exported globally. The shell, and any
command passed to the container, starts in the EC checkout,
`/workspace/src/platform/ec`.

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

```bash
./twister -ivc -s hibernate_z5.default
```

#### 2. Real Device Testing on Dagwood (using Helper Script)
To run tests against a Dagwood board and EC Add-in-card (AIC) connected to the
host, run `./dagwood` through the `run_docker.sh` helper script, which starts
the container with access to the host's USB devices and serial ports.
`./dagwood` locates the Dagwood board and its EC serial port and invokes
twister with the required parameters (toolchain, flash command, etc.).

Test results are available on your host filesystem at
`workspace/src/platform/ec/twister-out/`.

##### Realtek (RTS5912) Example
For Realtek boards, the container automatically builds and pre-installs the
`rtkupdate` utility and the `rts5915_flash_upload.bin` monitor binary on
startup.

To run all EC-AIC tests on `realtek/rts5912`:
```bash
./run_docker.sh ./dagwood -p realtek/rts5912
```

##### Nuvoton (NPCX9) Example
For Nuvoton boards, the container automatically builds and pre-installs the
`uartupdatetool` utility and the `npcx_monitor.bin` monitor binary on
startup.

To run all EC-AIC tests on `npcx9/npcx9m7f`:
```bash
./run_docker.sh ./dagwood -p npcx9/npcx9m7f
```

##### Customizing the Run
You can override the defaults using the `./dagwood` options:

*   **Specify one or more test directories**:
    ```bash
    ./run_docker.sh ./dagwood -p realtek/rts5912 -T zephyr/test/ec-aic -T zephyr/test/another-test
    ```
*   **Run one or more specific test scenarios**:
    ```bash
    ./run_docker.sh ./dagwood -p realtek/rts5912 -s aic.i2c -s another.scenario
    ```
*   **Specify a different serial port**:
    ```bash
    ./run_docker.sh ./dagwood -p realtek/rts5912 -d /dev/ttyACM0
    ```

For help on options:
```bash
./run_docker.sh ./dagwood -h
```

### Direct Command Execution (Without Entering Container)
You can trigger builds or run tests directly from your host machine:

**For builds:**
```bash
docker compose run --rm shell zmake --checkout /workspace build skyrim
```

**For Twister tests:**
```bash
docker compose run --rm shell ./twister -ivc -s hibernate_z5.default
```

Or using `run_docker.sh`:
```bash
./run_docker.sh zmake --checkout /workspace build skyrim
./run_docker.sh ./twister -ivc -s hibernate_z5.default
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

---

## Named Compose Services

`compose.yaml` also defines named services for common tasks. They can be
run directly with `docker compose run --rm <service>`:

| Task | Command |
|---|---|
| Build EC firmware | `docker compose run --rm zmake build skyrim` |
| Host-based emulation tests | `docker compose run --rm twister -ivc -s hibernate_z5.default` |
| Test on Dagwood | `docker compose run --rm dagwood -p realtek/rts5912` |
| Flash an EC image onto the AIC | `docker compose run --rm flash-dagwood-aic -b <project>` |
| List Dagwood boards | `docker compose run --rm flash-dagwood-aic -l` |
| Build and flash the Dagwood fixture firmware | `docker compose run --rm flash-dagwood-fixture` |
