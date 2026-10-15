# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE src/main.c)

# common/spi_flash_reg.c (compiled into app) selects its chip-specific
# protection range table via this macro (on the device it comes from the
# chip's flash_chip.h).  Define it only for this test scenario, where the
# table must match the GD25Q16 flash of the RTS5817 firmware.
target_compile_definitions(app PRIVATE CONFIG_SPI_FLASH_GD25Q16)
