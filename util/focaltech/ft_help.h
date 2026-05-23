/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FT_HELP_H_
#define FT_HELP_H_

#include <string_view>

namespace ft_help {
inline constexpr std::string_view kCmdHelpString = R"(
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