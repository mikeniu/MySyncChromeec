/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_FT_HELP_H_
#define UTIL_FOCALTECH_FT_HELP_H_

#include <string_view>

namespace focaltech {

constexpr std::string_view kToolVersion = "v1.0.5";

inline constexpr std::string_view kCmdHelpString = R"(
Usage: {} <command> [arguments]

Commands:
  <firmware.bin>         Update firmware using the specified .bin file.
                         (Requires ROM or Bootloader mode).
  enter_bootloader       Command the MCU to enter bootloader mode.
                         (Valid from EC mode: PID 0x5403, 0x5404).
  enter_rom              Command the MCU to enter ROM boot mode.
                         (Valid from test FW [0xFACE] or bootloader).
  flash_get_protect      Get the current flash write protection area.
                         (Requires bootloader mode).
  flash_set_protect <N>  Set the flash write protection area.
                         (Requires bootloader mode).
                         Levels (1-4):
                           1: Disable all write protection.
                           2: Disable up to the bootloader.
                           3: Disable up to RO.
                           4: Disable up to RW.
  get_ec_version         Read and display the current EC build version.

Options:
  -h, --help             Show this help message and exit.
)";
}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_HELP_H_
