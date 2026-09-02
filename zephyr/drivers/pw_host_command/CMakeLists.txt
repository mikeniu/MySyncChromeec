# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_build/pigweed.cmake)
include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_protobuf_compiler/proto.cmake)
include(${ZEPHYR_PIGWEED_MODULE_DIR}/pw_log_string/backend.cmake)

if(DEFINED CONFIG_PLATFORM_EC_PW_HOST_COMMAND_MAX_PAYLOAD_SIZE)
  set(PW_HOST_COMMAND_MAX_PAYLOAD_SIZE ${CONFIG_PLATFORM_EC_PW_HOST_COMMAND_MAX_PAYLOAD_SIZE})
else()
  set(PW_HOST_COMMAND_MAX_PAYLOAD_SIZE 512 CACHE STRING "")
endif()

configure_file(proto/host_command.options.in proto/host_command.options @ONLY)
file(
  COPY
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/host_command.proto"
  DESTINATION
    "${CMAKE_CURRENT_BINARY_DIR}/proto"
)

if(NOT DEFINED CONFIG_NANOPB)
  message(DEBUG "pw_host_command requires nanopb")
  return()
endif()

pw_proto_library(pw_host_command.proto
  SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/proto/host_command.proto"
  INPUTS
    "${CMAKE_CURRENT_BINARY_DIR}/proto/host_command.options"
  STRIP_PREFIX
    "${CMAKE_CURRENT_BINARY_DIR}"
  PREFIX
    "pw_host_command"
  DEPS
    pw_third_party.nanopb.proto
)

pw_add_library(pw_host_command.service INTERFACE
  HEADERS
    public/pw_host_command/service.h
  PUBLIC_INCLUDES
    public
  PUBLIC_DEPS
    pw_host_command.proto.nanopb_rpc
    pw_log
    pw_status
    cros_ec_interface
)

pw_add_test(pw_host_command.service_test
  SOURCES
    test_host_command_service.cc
  PRIVATE_DEPS
    pw_host_command.service
    pw_rpc.nanopb.test_method_context
    pw_unit_test.light
    pw_unit_test.logging_event_handler
    zephyr_interface
    cros_ec_interface
)

if(CONFIG_PLATFORM_EC_PW_HOST_COMMAND_UART_SERVER)
  pw_add_library(pw_host_command.log_backend INTERFACE
    PUBLIC_DEPS
      zephyr_interface
  )
  pw_set_backend(pw_log_string.handler pw_host_command.log_backend)
  zephyr_link_libraries(pw_log_string)

  zephyr_library()
  zephyr_library_compile_options(-fno-threadsafe-statics -fno-exceptions -fno-rtti -Wno-unused-local-typedefs)
  zephyr_library_sources(uart_server.cc)
  zephyr_library_link_libraries(
    pw_host_command.service
    pw_hdlc.decoder
    pw_hdlc.encoder
    pw_hdlc.rpc_channel_output
    pw_hdlc.default_addresses
    pw_log_string.handler
    pw_rpc.server
    cros_ec_interface
  )
endif()
