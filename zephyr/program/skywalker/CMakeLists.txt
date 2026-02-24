# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

target_sources(app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/i2c.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hibernate.c
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/hooks.c)
target_sources_ifndef(CONFIG_BOARD_SKYWALKER_SINGLE_USB_TYPEC app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/dp_alt_mode.c)

target_sources_ifdef(CONFIG_BOARD_SKYWALKER_PMIC_RESETB_IRQ app PRIVATE
	${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/pmic_resetb_irq.c)

if(DEFINED CONFIG_BOARD_SKYWALKER)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(skywalker)
	add_subdirectory(skywalker)
elseif(DEFINED CONFIG_BOARD_LUUKE)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(luuke)
	add_subdirectory(luuke)
elseif(DEFINED CONFIG_BOARD_OBIWAN)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(obiwan)
	add_subdirectory(obiwan)
elseif(DEFINED CONFIG_BOARD_YODA)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(yoda)
	add_subdirectory(yoda)
elseif(DEFINED CONFIG_BOARD_ANAKIN)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(anakin)
	add_subdirectory(anakin)
elseif(DEFINED CONFIG_BOARD_BAZE)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(baze)
	add_subdirectory(baze)
elseif(DEFINED CONFIG_BOARD_TARKIN)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(tarkin)
	add_subdirectory(tarkin)
elseif(DEFINED CONFIG_BOARD_PADME)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(padme)
	add_subdirectory(padme)
elseif(DEFINED CONFIG_BOARD_GROGU)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(grogu)
	add_subdirectory(grogu)
elseif(DEFINED CONFIG_BOARD_DOOKU)
	target_sources(app PRIVATE
		${PLATFORM_EC_PROGRAM_DIR}/skywalker/src/backlight.c)
	project(dooku)
	add_subdirectory(dooku)
elseif(DEFINED CONFIG_BOARD_VADER)
	project(vader)
	add_subdirectory(vader)
elseif(DEFINED CONFIG_BOARD_JAINA)
	project(jaina)
	add_subdirectory(jaina)
endif()
