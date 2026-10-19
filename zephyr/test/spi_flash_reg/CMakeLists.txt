# Copyright 2025 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(spi_flash_reg)

set(PLATFORM_EC "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(PLATFORM_EC "${PLATFORM_EC}" REALPATH)

target_sources(testbinary
  PRIVATE
    src/main.c
    ${PLATFORM_EC}/common/spi_flash_reg.c
)

configure_file(
    ${PLATFORM_EC}/include/spi_flash_reg.h
    ${CMAKE_BINARY_DIR}/include/spi_flash_reg.h COPYONLY
)
target_include_directories(testbinary
  PRIVATE
    ${CMAKE_BINARY_DIR}/include include/
)

# SPI_FLASH_CHIP selects which protect range table of spi_flash_reg.c is
# built and tested. It is set by the scenario's extra_args in testcase.yaml.
if(NOT DEFINED SPI_FLASH_CHIP)
  set(SPI_FLASH_CHIP W25Q64)
endif()

if(SPI_FLASH_CHIP STREQUAL "W25Q64")
  # W25Q64 has 0x400000 bytes as the first section, setting
  # CONFIG_FLASH_SIZE_BYTES to that allows us to test every branch of
  # spi_flash_reg.c
  set(SPI_FLASH_SIZE_BYTES 4194304)
elseif(SPI_FLASH_CHIP STREQUAL "GD25Q16")
  set(SPI_FLASH_SIZE_BYTES 2097152)
else()
  message(FATAL_ERROR "Unsupported SPI_FLASH_CHIP: ${SPI_FLASH_CHIP}")
endif()

target_compile_definitions(testbinary
  PUBLIC
    CONFIG_SPI_FLASH_${SPI_FLASH_CHIP}
    CONFIG_FLASH_SIZE_BYTES=${SPI_FLASH_SIZE_BYTES}
)
