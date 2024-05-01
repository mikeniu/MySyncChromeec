<<<<<<< HEAD   (3cb0f4c51a8bf64f21989a4f0bae67169d0661f2 jacuzzi: enable Try.SRC by default)
||||||| BASE   (5b42e4670d58fc19e82da4e955ef06fbc67bfd66 tcpmv2: Don't call tcpm_debug_detach during PRS)
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
    src/adc.c
    src/battery_cut_off.c
    src/battery_config.c
    src/battery_display_soc.c
    src/battery_v2.c
    src/charge_manager.c
    src/get_cmd_versions.c
    src/get_panic_info.c
    src/get_pd_port_caps.c
    src/host_command_test_protocol.c
    src/host_command.c
    src/host_event_commands.c
    src/host_event_commands_deprecated.c
    src/host_request_expected_size.c
    src/keyboard_mkbp.c
    src/motion_sense.c
    src/pd_chip_info.c
    src/pd_control.c
    src/pd_log.c
    src/tablet_mode.c
    src/usb_pd_control.c
    src/usb_pd_host_cmd.c
    src/ap_hang_detect.c
)

add_subdirectory(
    ${PLATFORM_EC}/zephyr/test/drivers/mock_panic_output
    mock_panic_output
)

zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD src/system.c)
=======
# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
    src/adc.c
    src/battery_cut_off.c
    src/battery_config.c
    src/battery_display_soc.c
    src/battery_v2.c
    src/console_print.c
    src/charge_manager.c
    src/get_cmd_versions.c
    src/get_panic_info.c
    src/get_pd_port_caps.c
    src/host_command_test_protocol.c
    src/host_command.c
    src/host_event_commands.c
    src/host_event_commands_deprecated.c
    src/host_request_expected_size.c
    src/keyboard_mkbp.c
    src/motion_sense.c
    src/pd_chip_info.c
    src/pd_control.c
    src/pd_log.c
    src/tablet_mode.c
    src/usb_pd_control.c
    src/usb_pd_host_cmd.c
    src/ap_hang_detect.c
)

add_subdirectory(
    ${PLATFORM_EC}/zephyr/test/drivers/mock_panic_output
    mock_panic_output
)

zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD src/system.c)
>>>>>>> CHANGE (655c2c33c22933d7a492282882ec3781893d3521 host_cmd: Add console_print host command)
