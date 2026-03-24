<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

# USAGE FAULT on int overflow
zephyr_compile_options(-ftrapv)

# Include directory with POSIX headers from Zephyr.
# We could enable CONFIG_POSIX_API, but it selects so many features that are
# not necessary.
zephyr_include_directories(${ZEPHYR_BASE}/include/zephyr/posix)

if (DEFINED CONFIG_BOARD_BLOONCHIPPER)
	add_subdirectory(bloonchipper)
	project(bloonchipper)
endif()

if (DEFINED CONFIG_BASEBOARD_HELIPILOT)
	add_subdirectory(helipilot)
	project(helipilot)
endif()

if (DEFINED CONFIG_BOARD_ET171)
	add_subdirectory(et171)
	project(et171)
endif()

zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD "src/fp_transport.c")
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")

# USAGE FAULT on int overflow
zephyr_compile_options(-ftrapv)

# Include directory with POSIX headers from Zephyr.
# We could enable CONFIG_POSIX_API, but it selects so many features that are
# not necessary.
zephyr_include_directories(${ZEPHYR_BASE}/include/zephyr/posix)

if (DEFINED CONFIG_BOARD_BLOONCHIPPER)
	add_subdirectory(bloonchipper)
	project(bloonchipper)
endif()

if (DEFINED CONFIG_BASEBOARD_HELIPILOT)
	add_subdirectory(helipilot)
	project(helipilot)
endif()

zephyr_library_sources_ifdef(CONFIG_EC_HOST_CMD "src/fp_transport.c")
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
