/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UTIL_FOCALTECH_FT_HELP_H_
#define UTIL_FOCALTECH_FT_HELP_H_

#include <string_view>

namespace focaltech {

constexpr std::string_view kToolVersion = "v1.0.6";

inline constexpr std::string_view kCmdHelpString = R"(
Usage: {} <command> [arguments]

Commands:
  <firmware.bin>         Update firmware using the specified .bin file.
                         (Requires ROM or Bootloader mode).
  enter_rom              Command the MCU to enter ROM boot mode.
                         (Valid from bootloader mode).
  flash_get_protect      Get the current flash write protection area.
                         (Requires bootloader mode).
  flash_set_protect <area>  Set the flash write protection area.
                         (Requires bootloader mode).
                         Areas:
                           none: Disable all write protection.
                           boot: Protect only the bootloader.
                           ro:   Protect the bootloader and RO region.
                           all:  Protect the entire flash (Bootloader, RO,
                                 and RW).

Options:
  -h, --help             Show this help message and exit.
)";
}  // namespace focaltech

#endif  // UTIL_FOCALTECH_FT_HELP_H_
