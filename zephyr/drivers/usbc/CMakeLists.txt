# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_USBC_INTEL_ALTMODE intel_altmode.c)
zephyr_library_sources_ifdef(CONFIG_USBC_PDC_IT52XX pdc_it52xx.c ucsi_v3.c) #it5272_cmd.c: Console command read/write pdc, we seems needn't to support(but code have)?
zephyr_library_sources_ifdef(CONFIG_USBC_PDC_RTS54XX pdc_rts54xx.c ucsi_v3.c)
zephyr_library_sources_ifdef(CONFIG_USB_PDC_POWER_MGMT ucsi_v3.c)
zephyr_library_sources_ifdef(CONFIG_USBC_PDC_RTS54XX_CONSOLE_FW_UPDATER pdc_rts54xx_fwup.c)

zephyr_library_sources_ifdef(CONFIG_USBC_PDC_TPS6699X pdc_tps6699x.c tps6699x_cmd.c ucsi_v3.c)
zephyr_library_sources_ifdef(CONFIG_USBC_PDC_TPS6699X_CONSOLE_FW_UPDATER tps6699x_fwup.c)
