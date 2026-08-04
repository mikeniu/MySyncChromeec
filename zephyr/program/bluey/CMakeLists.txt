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

if(DEFINED CONFIG_BOARD_MICA)
	project(mica)
	add_subdirectory(mica)
endif()

if(DEFINED CONFIG_BOARD_ANNITE)
	project(annite)
	add_subdirectory(annite)
endif()

if(DEFINED CONFIG_BOARD_ANETO)
	project(aneto)
	add_subdirectory(aneto)
endif()

if(DEFINED CONFIG_BOARD_PIC)
	project(pic)
	add_subdirectory(pic)
endif()

if(DEFINED CONFIG_BOARD_MICA)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"mica/src/throttle.c")
elseif(DEFINED CONFIG_BOARD_ANNITE)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"annite/src/throttle.c")
elseif(DEFINED CONFIG_BOARD_ANETO)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"aneto/src/throttle.c")
elseif(DEFINED CONFIG_BOARD_PIC)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"pic/src/throttle.c")
else()
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"src/throttle.c")
endif()

if(DEFINED CONFIG_BOARD_MERU)
	project(meru)
	add_subdirectory(meru)
endif()

zephyr_library_sources("src/battery.c")
zephyr_library_sources("src/switchcap.c")
zephyr_library_sources("src/chipset.c")
