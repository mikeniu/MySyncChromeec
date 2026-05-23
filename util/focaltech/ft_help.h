/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FT_HELP_H_
#define FT_HELP_H_

#include <string_view>

namespace ft_help {
inline constexpr std::string_view kCmdHelpString = R"(
<<<<<<< PATCH SET (1453e788f70905e1537b7037445d9018459c2ba0 Convert the code to C++.  Remove USB suspend/resume function)
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
}

#endif
||||||| BASE      (15158824c8a1973786840dac479eeb84a7cc33a8 ec: fix missing includes)
=======
If the MCU is currently in ROM mode or running a test FW with PID 0xface, please execute the following instructions for the upgrade.
./focaltech_fingerprint_iap back_to_rom_boot
./focaltech_fingerprint_iap ec_full1201.bin

If the MCU is currently running FW with PID 0x5403 or 0x5404, please use the following command to enter bootloader mode.
./focaltech_fingerprint_iap back_to_bootloader

If the MCU is in bootloader mode (VID 2808: PID 0001), it can only upgrade FW with focaltech hash verification.
./focaltech_fingerprint_iap ec_hash_1201.bin

If the MCU is in bootloader mode (VID 2808: PID 0001), you can use the following command to revert to ROM mode.
./focaltech_fingerprint_iap back_to_rom_boot

Use the following command to disable flash write protection. Note that it can only be executed in bootloader mode.
1 represents disabling write protection for all flash addresses.
2 represents disabling from the starting address up to the bootloader.
3 represents disabling from the starting address up to RO.
4 represents disabling from the starting address up to RW.
./focaltech_fingerprint_iap flash_set_protect 1
)";
}

#endif
>>>>>>> BASE      (aea89f2ea6978aa9a7fe54dd61b110e76f30dea8 util: focaltech: Add focaltech IAP tool)
