# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(input_ite_it8801_kbd)

# Add test source files
target_sources(app PRIVATE
    src/main.c
    src/it8801_emul.c
    src/fake_altctrl.c
    src/stubs.c
)

# Add include directory for test headers
target_include_directories(app PRIVATE include)
