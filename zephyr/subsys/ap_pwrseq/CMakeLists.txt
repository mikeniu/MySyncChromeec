# SPDX-License-Identifier: Apache-2.0

zephyr_library_include_directories(include)

zephyr_library_sources_ifdef(CONFIG_AP_EVENTS ap_events.c)

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ
	ap_power_interface.c
	)

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_POWER_SIGNALS power_signals.c)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_SIGNAL_GPIO signal_gpio.c)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_SIGNAL_VW signal_vw.c)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_SIGNAL_ADC signal_adc.c)

if (NOT DEFINED CONFIG_EMUL_AP_PWRSEQ_DRIVER)
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_DRIVER ap_pwrseq.c)
endif()
zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_DRIVER ap_pwrseq_sm.c)

zephyr_library_sources_ifdef(CONFIG_AP_PWRSEQ_HOST_SLEEP power_host_sleep.c)

add_subdirectory_ifdef(CONFIG_X86_NON_DSX_PWRSEQ x86_non_dsx)

if (DEFINED CONFIG_AP_PWRSEQ)
	zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_AP_RESET_LOG
		ap_reset_log.c)
endif ()
