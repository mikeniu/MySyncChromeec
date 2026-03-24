<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_IT8XXX2	cros_flash_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_NPCX	cros_flash_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_XEC	cros_flash_xec.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH	        cros_flash.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_BACKEND_STM32
	                                                cros_flash_stm32_backend.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_RTK cros_flash_rtk.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_MCHP_INTERNAL_WP cros_flash_mchp_wp.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_EM32F967 cros_flash_em32f967.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_ANDES_XIP
	                                                cros_flash_andestech_xip.c)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_IT8XXX2	cros_flash_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_NPCX	cros_flash_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_XEC	cros_flash_xec.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH	        cros_flash.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_BACKEND_STM32
	                                                cros_flash_stm32_backend.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_RTK cros_flash_rtk.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_MCHP_INTERNAL_WP cros_flash_mchp_wp.c)
zephyr_library_sources_ifdef(CONFIG_CROS_FLASH_EM32F967 cros_flash_em32f967.c)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
