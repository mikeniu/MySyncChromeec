# Copyright 2021 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

zephyr_include_directories(include)
if(NOT DEFINED CONFIG_BOARD_INTEL_ISH_5_4_1)
	zephyr_library_sources("src/common.c")
endif()
zephyr_library_sources_ifdef(CONFIG_NISSA_SUB_BOARD "src/sub_board.c")
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ "src/board_power.c")

if(DEFINED CONFIG_BOARD_NIVVIKS)
	project(nivviks)
	zephyr_library_sources(
			"src/nivviks_led.c"
			"src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_raa489000.c")
endif()
if(DEFINED CONFIG_BOARD_NEREID)
	project(nereid)
	zephyr_library_sources(
			"src/led.c"
			"src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nereid/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
endif()
if(DEFINED CONFIG_BOARD_NOKRIS)
	project(nokris)
	zephyr_library_sources(
			"src/nivviks_led.c"
			"src/form_factor.c"
			"src/hdmi.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "nokris/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nokris_charger.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASK)
	zephyr_library_sources(
			"src/fan.c"
			"craask/src/form_factor.c"
			"craask/src/keyboard.c"
			"src/kb_backlight.c"
			"craask/src/led.c"
			"craask/src/thermal.c"
			"src/touch_panel.c"
	)
	project(craask)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craask/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craask/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJO)
	project(pujjo)
	zephyr_library_sources(
			"pujjo/src/led.c"
			"pujjo/src/hdmi.c"
			"pujjo/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "pujjo/src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjo/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "pujjo/src/charger.c")
endif()
if(DEFINED CONFIG_BOARD_XIVU)
	project(xivu)
	zephyr_library_sources(
			"xivu/src/form_factor.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "xivu/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_raa489000.c")
endif()
if(DEFINED CONFIG_BOARD_JOXER)
	project(joxer)
	zephyr_library_sources(
			"joxer/src/board.c"
			"joxer/src/led.c"
			"joxer/src/keyboard.c"
			"src/form_factor.c"
			"joxer/src/sub_board.c"
			"joxer/src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "joxer/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "joxer/src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVIKS)
	project(yaviks)
	zephyr_library_sources(
			"yaviks/src/led.c"
			"yaviks/src/keyboard.c"
			"yaviks/src/board.c"
			"src/yaviks_sub_board.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/yaviks_usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVILLA)
	project(yavilla)
	zephyr_library_sources(
			"yavilla/src/led.c"
			"yavilla/src/keyboard.c"
			"yavilla/src/board.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_YAVISTA)
	project(yavista)
	zephyr_library_sources(
			"yavista/src/led.c"
			"yavista/src/keyboard.c"
			"yavista/src/board.c"
			"src/yaviks_sub_board.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/yaviks_usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_ULDREN)
	project(uldren)
	zephyr_library_sources(
			"uldren/src/form_factor.c"
			"src/kb_backlight.c"
			"uldren/src/sub_board.c"
			"uldren/src/mp2964_update.c"
			"uldren/src/sensor.c"
			"uldren/src/usb_mux_config.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "uldren/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_raa489000.c")
endif()
if(DEFINED CONFIG_BOARD_GOTHRAX)
	project(gothrax)
	zephyr_library_sources(
			"src/hdmi.c"
			"src/gothrax_usb_mux_config.c"
			"src/form_factor.c"
			"src/gothrax_sub_board.c"
			"src/gothrax_thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER
			"src/charger_raa489000.c"
			"src/gothrax_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
endif()
if(DEFINED CONFIG_BOARD_CRAASKOV)
	project(craaskov)
	zephyr_include_directories("craaskov/include")
	zephyr_library_sources(
			"craaskov/src/board.c"
			"craaskov/src/keyboard.c"
			"craaskov/src/hdmi.c"
			"src/touch_panel.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "craaskov/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "craaskov/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"craaskov/src/keyboard.c"
	"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_QUANDISO)
	project(quandiso)
	zephyr_library_sources(
			"quandiso/src/board.c"
			"src/quandiso_sub_board.c"
			"quandiso/src/keyboard.c"
			"quandiso/src/led.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_sm5803.c")
endif()
if(DEFINED CONFIG_BOARD_QUANDISO2)
	project(quandiso2)
	zephyr_library_sources(
			"quandiso2/src/board.c"
			"src/quandiso_sub_board.c"
			"quandiso2/src/keyboard.c"
			"quandiso2/src/led.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_sm5803.c")
endif()
if(DEFINED CONFIG_BOARD_PIRRHA)
	project(pirrha)
	zephyr_include_directories("pirrha/include")
	zephyr_library_sources(
			"pirrha/src/led.c"
			"pirrha/src/board.c"
			"pirrha/src/battery.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_raa489000.c")
endif()
if(DEFINED CONFIG_BOARD_ANRAGGAR)
	project(anraggar)
	zephyr_include_directories("anraggar/include")
	zephyr_library_sources(
			"anraggar/src/board.c"
			"src/anraggar_sub_board.c"
			"src/prochot.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "anraggar/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_bq25710.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_GLASSWAY)
	project(glassway)
	zephyr_library_sources(
			"src/glassway_sub_board.c"
			"src/glassway_kb_backlight.c"
			"glassway/src/board.c"
			"src/glassway_usb_mux_config.c"
			"glassway/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/glassway_fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER
			"src/charger_raa489000.c"
			"src/glassway_charger.c"
	)
endif()
if(DEFINED CONFIG_BOARD_SUNDANCE)
	project(sundance)
	zephyr_library_sources(
			"sundance/src/led.c"
			"src/form_factor.c"
			"sundance/src/board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "sundance/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nokris_charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJOGA)
	project(pujjoga)
	zephyr_library_sources(
			"pujjoga/src/board.c"
			"pujjoga/src/led.c"
			"src/pujjoga_sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjoga/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nokris_charger.c")
endif()
if(DEFINED CONFIG_BOARD_PUJJOGATWIN)
	project(pujjogatwin)
	zephyr_library_sources(
			"pujjogatwin/src/board.c"
			"pujjogatwin/src/led.c"
			"src/pujjoga_sub_board.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjogatwin/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/nokris_charger.c")
endif()
if(DEFINED CONFIG_BOARD_RIVEN)
	zephyr_include_directories("riven/include")
	zephyr_library_sources(
			"riven/src/form_factor.c"
			"riven/src/led.c"
			"src/touch_panel.c"
			"riven/src/usb_mux_config.c"
	)
	project(riven)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "riven/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "riven/src/charger.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
	"riven/src/keyboard.c"
	"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_DOMIKA)
	project(domika)
	zephyr_library_sources(
			"domika/src/led.c"
			"domika/src/keyboard.c"
			"domika/src/board.c"
			"src/thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_sm5803.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")

endif()
if(DEFINED CONFIG_BOARD_TELIKS)
	project(teliks)
	zephyr_include_directories("teliks/include")
	zephyr_library_sources(
			"teliks/src/board.c"
			"src/anraggar_sub_board.c"
			"src/prochot.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/teliks_usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_bq25710.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_RULL)
	project(rull)
	zephyr_include_directories("rull/include")
	zephyr_library_sources(
			"rull/src/board.c"
			"src/anraggar_sub_board.c"
			"rull/src/usb_mux_config.c"
			"src/touch_panel.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "rull/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_bq25710.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"rull/src/keyboard_customization.c"
			"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_TELITH)
	project(telith)
	zephyr_include_directories("telith/include")
	zephyr_library_sources(
			"telith/src/board_als.c"
			"telith/src/board.c"
			"src/prochot.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/teliks_usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_bq25710.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"telith/src/keyboard_customization.c"
			"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_PUJJONIRU)
	project(pujjoniru)
	zephyr_include_directories("pujjoniru/include")
	zephyr_library_sources(
			"pujjoniru/src/board.c"
			"src/touch_panel.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "pujjoniru/src/usbc.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_bq25710.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD_CUSTOMIZATION
			"src/keyboard_customization.c"
	)
endif()
if(DEFINED CONFIG_BOARD_GUREN)
	project(guren)
	zephyr_library_sources(
			"src/glassway_sub_board.c"
			"src/glassway_kb_backlight.c"
			"guren/src/board.c"
			"src/glassway_usb_mux_config.c"
			"guren/src/keyboard.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/glassway_fan.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER
			"src/charger_raa489000.c"
			"src/glassway_charger.c"
	)
endif()
if(DEFINED CONFIG_BOARD_DIRKS)
	project(dirks)
	zephyr_include_directories("dirks/include")
	zephyr_library_sources(
			"dirks/src/barrel_jack.c"
			"dirks/src/led.c"
			"src/hdmi.c"
			"src/dirks_thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/dirks_usbc.c")
endif()
if(DEFINED CONFIG_BOARD_MELIKS)
	project(meliks)
	zephyr_include_directories("meliks/include")
	zephyr_library_sources(
			"meliks/src/led.c"
			"meliks/src/board.c"
			"meliks/src/battery.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER "src/charger_raa489000.c")
endif()
if(DEFINED CONFIG_BOARD_EPIC)
	project(epic)
	zephyr_library_sources(
			"src/hdmi.c"
			"src/gothrax_usb_mux_config.c"
			"src/form_factor.c"
			"src/gothrax_sub_board.c"
			"src/gothrax_thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/usbc_raa489000.c")
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CHARGER
			"src/charger_raa489000.c"
			"src/gothrax_charger.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN "src/fan.c")
endif()

if(CONFIG_SOC_IT8XXX2_KERNEL_IN_RAM AND CONFIG_KERNEL_NO_LTO)
	# Non-critical kernel source files that may use LTO to reduce RAM usage
	set(KERNEL_LTO_FILES
		${ZEPHYR_BASE}/kernel/init.c
		${ZEPHYR_BASE}/kernel/errno.c
		${ZEPHYR_BASE}/kernel/fatal.c
		${ZEPHYR_BASE}/kernel/timer.c
	)
	set_source_files_properties(${KERNEL_LTO_FILES} TARGET_DIRECTORY kernel PROPERTIES
		COMPILE_FLAGS $<TARGET_PROPERTY:compiler,optimization_lto>
	)
endif()
if(DEFINED CONFIG_BOARD_DIRKSON)
	project(dirkson)
	zephyr_include_directories("dirkson/include")
	zephyr_library_sources(
			"dirkson/src/barrel_jack.c"
			"dirkson/src/led.c"
			"src/hdmi.c"
			"src/dirks_thermal.c"
	)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_USBC "src/dirks_usbc.c")
endif()
