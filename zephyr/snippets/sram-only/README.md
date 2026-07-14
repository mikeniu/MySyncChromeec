# sram-only Snippet

This snippet configures the Zephyr binary for loading and execution directly
from SRAM.

This snippet can be enabled a few different ways.

1. **Downstream on-device tests**.  Configure on device tests defined in the
`platform/ec` repository for SRAM only operation by adding the `SNIPPET`
to the `extra_args` property.

    ```yaml
    tests:
      aic.i2c:
        extra_args: SNIPPET="sram-only"
        depends_on:
          - i2c
    ```

1. **Upstream on-device tests**. You can configure an upstream on device test
for SRAM only operation by defining the `SNIPPET` CMake property directly on
the `twister` command line option.

    ```bash
    ./twister -p realtek/rts5912 -s kernel.poll <...> -x=SNIPPET=sram-only
    ```

    See the [EC Add-in-card (AIC) Tests] for additional details about running
on device tests.

1. For full EC images, add or append `sram-only` to the `snippets` parameter in
your project's BUILD.py file.

    ```python
    register_rtk_project(
        project_name="minimal-realtek",
        zephyr_board="realtek/rts5912",
        dts_overlays=[here / "realtek.dts"],
        kconfig_files=[here / "realtek.conf"],
        snippets=["sram-only],
    )
    ```
    Note that you should use the `build/zephyr/<project>/build-ro/zephyr/zephyr.bin`
binary when loading the image onto the target board using the vendor update tool
(`uartupdatetool` or `rtkupdate`).

    Generally, loading full EC images directly in SRAM should only be done for
local debugging.

[EC Add-in-card (AIC) Tests]:
