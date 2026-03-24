<<<<<<< HEAD   (453fd99d5363bf9651b2fb0d3ffbbab7386a6376 program,zmake: drop cmsis references)
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS "$ENV{ZEPHYR_BASE}")
project(lid_angle_common)

target_sources(testbinary PRIVATE
  fakes.c
  main.c
  ../../../common/lid_angle_keyboard_policy.c
)
target_include_directories(testbinary PRIVATE
  include
  ../../../include
  ../../../fuzz
)
||||||| BASE   (0cb93ccd933607defa9592c423c9a9fb4946fd4d anakin: Set PDC source PDO overcurrent capability)
# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr COMPONENTS unittest REQUIRED HINTS "$ENV{ZEPHYR_BASE}")
project(lid_angle_common)

target_sources(testbinary PRIVATE
  fakes.c
  main.c
  ../../../common/lid_angle_common.c
)
target_include_directories(testbinary PRIVATE
  include
  ../../../include
  ../../../fuzz
)
=======
>>>>>>> BRANCH (86da3bbeafeffe5727f5bfec7d9eed87995f2661 UPSTREAM: driver: add new ALS CM36781 driver code)
