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

<<<<<<< HEAD   (6de6ff8287d9bedd635f754381c03aec105d13ae fpsensor: Prevent integer overflow in get_frame buffer acces)
||||||| BASE   (2e8c032bba3106e3dc82d9f0267bdb589d7bda01 RTS5915: reserved bbram for first-battery-plug)
if(DEFINED CONFIG_BOARD_MICA)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"mica/src/throttle.c")
else()
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"src/throttle.c")
endif()

=======
if(DEFINED CONFIG_BOARD_ANNITE)
	project(annite)
	add_subdirectory(annite)
endif()

if(DEFINED CONFIG_BOARD_MICA)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"mica/src/throttle.c")
elseif(DEFINED CONFIG_BOARD_ANNITE)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"annite/src/throttle.c")
else()
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP
					"src/throttle.c")
endif()

>>>>>>> CHANGE (6d23069302b5d107cfe33af55677e471cc32e774 bluey: Add initial Annite project)
zephyr_library_sources("src/battery.c")
zephyr_library_sources("src/switchcap.c")
zephyr_library_sources("src/chipset.c")
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_THROTTLE_AP "src/throttle.c")
