<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_IT8XXX2	cros_system_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_NPCX	cros_system_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_XEC	cros_system_xec.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_NATIVE_POSIX	cros_system_native_posix.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_STM32	cros_system_stm32.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_REALTEK cros_system_rtk.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_ET171	cros_system_et171.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_ELAN_EM32    cros_system_em32.c)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_IT8XXX2	cros_system_it8xxx2.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_NPCX	cros_system_npcx.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_XEC	cros_system_xec.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_NATIVE_POSIX	cros_system_native_posix.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_STM32	cros_system_stm32.c)
zephyr_library_sources_ifdef(CONFIG_CROS_SYSTEM_REALTEK cros_system_rtk.c)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
