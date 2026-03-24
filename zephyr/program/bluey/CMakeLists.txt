<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_BLUEY)
	project(bluey)
	add_subdirectory(bluey)
endif()

if(DEFINED CONFIG_BOARD_QUENBI)
	project(quenbi)
	add_subdirectory(quenbi)
endif()

if(DEFINED CONFIG_BOARD_QUARTZ)
	project(quartz)
	add_subdirectory(quartz)
endif()

zephyr_library_sources("src/battery.c")
zephyr_library_sources("src/switchcap.c")
zephyr_library_sources("src/chipset.c")
zephyr_library_sources("src/adsp_i2c_mode.c")
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_BLUEY)
	project(bluey)
	add_subdirectory(bluey)
endif()

if(DEFINED CONFIG_BOARD_QUENBI)
	project(quenbi)
	add_subdirectory(quenbi)
endif()

if(DEFINED CONFIG_BOARD_QUARTZ)
	project(quartz)
	add_subdirectory(quartz)
endif()

zephyr_library_sources("src/switchcap.c")
zephyr_library_sources("src/chipset.c")
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
