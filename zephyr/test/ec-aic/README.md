# EC Add-in-card (AIC) Tests

The tests in this directory are compiled and run on the EC AIC connected to the
Dagwood tester.

Test output is generated from the EC UART, sent through the UART to USB bridge
in the Dagwood firmware, and consumed by twister running on the host system.

## Building and running tests

A wrapper script `./dagwood` is provided in the `platform/ec` root directory. It
simplifies testing by automatically detecting connected Dagwood boards,
configuring the required toolchain, device testing parameters, and flash
commands.

This is currently only supported in the chroot environment, and you must also
have the dagwood repository available.

All the commands must be executed from the chroot environment and the current
directory must be `~/chromiumos/src/platform/ec`

### Dagwood hardware setup

1. Connect an EC AIC (ITE, Nuvoton, or Realtek) to your Dagwood board.
1. Connect a Dagwood board to your host system using a USB Type-C cable.
1. Update the Dagwood firmware by following the instructions in the [Dagwood
README].
1. After flashing the Dagwood firmware, reboot the Dagwood board by pressing
the `NRST` reset button.
1. Verify the Dagwood board is detected and obtain its board ID. This can be
run inside or outside the chroot:
    ```bash
    $ ../dagwood/flash.py -l
    Board id: 3635383134325119001C003A version: v0.0.1
    ```
    Alternatively, verify the Dagwood TTY ports under `/dev`:
    ```bash
    $ find /dev -name "ttyACM*"
    /dev/ttyACM2
    /dev/ttyACM1
    /dev/ttyACM0
    ```

### Building the tests only
Use Twister's `-b` option to build the tests only. All unknown arguments are
passed directly to Twister.

```bash
./dagwood -T zephyr/test/ec-aic -p realtek/rts5912 -b
```

### Building and running all the Dagwood tests

Use the `-T zephyr/test/ec-aic` option to run all tests targeting EC AIC boards.

This assumes you only have one Dagwood board connected to your host machine.

```bash
./dagwood -T zephyr/test/ec-aic -p realtek/rts5912
```

### Running a single test
You can run just a single test by using the `-s <test_name>` option. Valid test
names are found in the `testcase.yaml` files under the `zephyr/test/ec-aic`
directory.

```bash
./dagwood -s aic.i2c -p realtek/rts5912
```

### Running on a specific Dagwood board
When multiple Dagwood boards are connected to your host, specify the target board
using `--board-id <board_id>`. The script automatically identifies the
corresponding EC serial console port and configures the flash tool.

```bash
./dagwood -s aic.i2c -p realtek/rts5912 --board-id 3635383134325119001C003A
```

### Running the tests from SRAM
By default, the dagwood script reprograms the integrated SPI flash
with the test binary. On Nuvoton and Realtek platforms, the test binaries can
be loaded directly into the on-chip SRAM.

This has 2 main benefits:

* Faster test time because the flash erase and write steps are skipped
* Avoids wearing out the flash prematurely

To run the tests from SRAM, append the `--sram` flag.

```bash
./dagwood -s aic.i2c -p npcx9/npcx9m7f --sram
```

Note that any test binaries that use `sysjump` or have other dependencies on
the SPI flash should not use the `--sram` option.

The `--sram` option is ignored if the EC architecture doesn't support loading
directly into SRAM.

## Running tests on multiple Dagwood boards

To run tests on multiple Dagwood boards connected to the host, use the
[`./dagwood-hwmap`] file. You need to edit this file to specify the
board ID and serial device path for each Dagwood connected.

A file header comment in [`./dagwood-hwmap`] provides details on how to modify
the hardware map.

To build and run the tests against all boards, use the `--hardware-map` option.

```bash
./twister -ivc -s aic.i2c --toolchain=coreboot-sdk \
  -p realtek/rts5912 -p npcx9/npcx9m7f \
  --device-testing --hardware-map zephyr/test/ec-aic/dagwood-hwmap \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

## Advanced usage: Running Twister directly

If you prefer to invoke `twister` manually instead of using `./dagwood`, you
must explicitly provide all device testing and runtime arguments.

### Building and running all the Dagwood tests
Flashing the EC and running the tests requires the `--device-testing` and the
`--device-serial /dev/ttyACM1` options.

```bash
./twister -ivc -T zephyr/test/ec-aic --toolchain=coreboot-sdk \
  -p realtek/rts5912 \
  --device-testing --device-serial /dev/ttyACM1 \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

### Running a single test
```bash
./twister -ivc -s aic.i2c --toolchain=coreboot-sdk \
  -p realtek/rts5912 \
  --device-testing --device-serial /dev/ttyACM1 \
  --flash-command ../dagwood/flash.py --device-flash-timeout 60
```

### Running the tests from SRAM
To run the tests from SRAM manually, use the same twister command but add the
`-r` parameter to the `../dagwood/flash.py` script and apply the SRAM
configuration snippet. This snippet parameter is automatically ignored if the
platform doesn't support the snippet.

```bash
./twister -ivc -s aic.i2c --toolchain=coreboot-sdk \
  -x=SNIPPET=sram-only \
  -p npcx9/npcx9m7f \
  --device-testing --device-serial /dev/ttyACM1 \
  --flash-command="../dagwood/flash.py,-r" --device-flash-timeout 60
```

[Twister]: https://docs.zephyrproject.org/latest/develop/test/twister.html
[Dagwood README]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform/dagwood/README.md
[`./dagwood-hwmap`]: ./dagwood-hwmap
