<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/i2c.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
target_sources_ifndef(CONFIG_BOARD_SKYWALKER_SINGLE_USB_TYPEC app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/dp_alt_mode.c)

target_sources_ifdef(CONFIG_BOARD_SKYWALKER_PMIC_RESETB_IRQ app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/pmic_resetb_irq.c)

if(DEFINED CONFIG_BOARD_SKYWALKER)
	project(skywalker)
	add_subdirectory(skywalker)
elseif(DEFINED CONFIG_BOARD_LUUKE)
	project(luuke)
	add_subdirectory(luuke)
elseif(DEFINED CONFIG_BOARD_OBIWAN)
	project(obiwan)
	add_subdirectory(obiwan)
elseif(DEFINED CONFIG_BOARD_YODA)
	project(yoda)
	add_subdirectory(yoda)
elseif(DEFINED CONFIG_BOARD_ANAKIN)
	project(anakin)
	add_subdirectory(anakin)
elseif(DEFINED CONFIG_BOARD_BAZE)
	project(baze)
	add_subdirectory(baze)
elseif(DEFINED CONFIG_BOARD_TARKIN)
	project(tarkin)
	add_subdirectory(tarkin)
elseif(DEFINED CONFIG_BOARD_PADME)
	project(padme)
	add_subdirectory(padme)
elseif(DEFINED CONFIG_BOARD_GROGU)
	project(grogu)
	add_subdirectory(grogu)
elseif(DEFINED CONFIG_BOARD_DOOKU)
	project(dooku)
	add_subdirectory(dooku)
elseif(DEFINED CONFIG_BOARD_VADER)
	project(vader)
	add_subdirectory(vader)
endif()
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/i2c.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hooks.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
target_sources_ifndef(CONFIG_BOARD_SKYWALKER_SINGLE_USB_TYPEC app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/dp_alt_mode.c)

target_sources_ifdef(CONFIG_BOARD_SKYWALKER_PMIC_RESETB_IRQ app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/pmic_resetb_irq.c)

if(DEFINED CONFIG_BOARD_SKYWALKER)
	project(skywalker)
	add_subdirectory(skywalker)
elseif(DEFINED CONFIG_BOARD_LUUKE)
	project(luuke)
	add_subdirectory(luuke)
elseif(DEFINED CONFIG_BOARD_OBIWAN)
	project(obiwan)
	add_subdirectory(obiwan)
elseif(DEFINED CONFIG_BOARD_YODA)
	project(yoda)
	add_subdirectory(yoda)
elseif(DEFINED CONFIG_BOARD_ANAKIN)
	project(anakin)
	add_subdirectory(anakin)
elseif(DEFINED CONFIG_BOARD_BAZE)
	project(baze)
	add_subdirectory(baze)
elseif(DEFINED CONFIG_BOARD_TARKIN)
	project(tarkin)
	add_subdirectory(tarkin)
elseif(DEFINED CONFIG_BOARD_PADME)
	project(padme)
	add_subdirectory(padme)
elseif(DEFINED CONFIG_BOARD_GROGU)
	project(grogu)
	add_subdirectory(grogu)
endif()
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
