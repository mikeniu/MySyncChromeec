# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(fpsensor_task_test)

target_include_directories(app PUBLIC ${PLATFORM_EC}/common)
zephyr_include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")

target_sources(app PRIVATE src/mock_fingerprint_algorithm.c)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_CAPTURE app PRIVATE
	src/fpsensor_capture.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_ENROLL app PRIVATE
	src/fpsensor_enroll.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_FINGER_PRESENCE app PRIVATE
	src/fpsensor_finger_presence.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_GET_FRAME app PRIVATE
	src/fpsensor_get_frame.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_INIT app PRIVATE
	src/fpsensor_init.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_MATCH app PRIVATE
	src/fpsensor_match.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_SHIM app PRIVATE
	src/fpsensor_shim.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_STATE app PRIVATE
	src/fpsensor_state.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_TEMPLATE app PRIVATE
	src/fpsensor_template.cc
	${PLATFORM_EC}/test/mock/fpsensor_crypto_mock.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_UTILS app PRIVATE
	src/fpsensor_utils.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_DEBUG app PRIVATE
	src/fpsensor_debug.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_COMMANDS app PRIVATE
	src/fpsensor_vendor_command.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_ASCP app PRIVATE
	src/fpsensor_ascp.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_FRAME_SIZE app PRIVATE
	src/fpsensor_frame_size.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_BTN_IGN_OUT app PRIVATE
	src/fpsensor_btn_ign_out.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_LED app PRIVATE
	src/fpsensor_led.cc)
target_sources_ifdef(CONFIG_LINK_TEST_SUITE_FINGERPRINT_AUTH_COMMANDS app PRIVATE
	src/fpsensor_auth_commands.cc)
target_sources_ifdef(CONFIG_NPCX_OTP_KEY app PRIVATE
	${PLATFORM_EC}/chip/host/otp_key.c
	${PLATFORM_EC}/test/mock/otpi_mock.c)

add_subdirectory(${PLATFORM_EC}/zephyr/test/test_utils test_utils)
