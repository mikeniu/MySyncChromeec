# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.5)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

if(DEFINED CONFIG_BOARD_FATCATRVP_NPCX)
	project(fatcatrvp-npcx)
	add_subdirectory(fatcatrvp-npcx)
endif()

if(DEFINED CONFIG_BOARD_FATCATRVP_ITE)
	project(fatcatrvp-ite)
	add_subdirectory(fatcatrvp-ite)
endif()

if(DEFINED CONFIG_BOARD_FRANCKA)
	project(francka)
	add_subdirectory(francka)
	zephyr_include_directories("francka/include")
	zephyr_library_sources(
			"francka/src/touch_panel.c"
			"francka/src/board_power.c"
			"francka/src/alt_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"francka/src/keyboard_customization.c"
	)
endif()

if(DEFINED CONFIG_BOARD_FRTK)
	project(frtk)
	add_subdirectory(frtk)
	zephyr_include_directories("frtk/include")
	zephyr_library_sources(
			"frtk/src/touch_panel.c"
			"frtk/src/board_power.c"
			"frtk/src/alt_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"frtk/src/keyboard_customization.c"
	)
endif()

if(DEFINED CONFIG_BOARD_FELINO)
	if(DEFINED CONFIG_BOARD_FELINO4ES)
		project(felino4es)
		add_subdirectory(felino4es)
		zephyr_include_directories("felino4es/include")
		zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
						"felino4es/src/keyboard_customization.c")
	else()
		project(felino)
		add_subdirectory(felino)
		zephyr_include_directories("felino/include")
		zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
						"felino/src/keyboard_customization.c")
	endif()
endif()

if(DEFINED CONFIG_BOARD_KINMEN)
	project(kinmen)
	zephyr_include_directories("kinmen/include")
	zephyr_library_sources("kinmen/src/board_power.c")
	zephyr_library_sources("kinmen/src/suspend_resume.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
					"kinmen/src/keyboard_customization.c")
endif()
