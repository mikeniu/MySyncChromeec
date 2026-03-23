<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/dp_alt_mode.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/i2c.c)

if(DEFINED CONFIG_BOARD_TANJIRO)
	project(tanjiro)

elseif(DEFINED CONFIG_BOARD_SAPPHIRE)
	project(sapphire)
	add_subdirectory(sapphire)
endif()
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/tanjiro/src/i2c.c)

if(DEFINED CONFIG_BOARD_TANJIRO)
	project(tanjiro)
endif()
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
