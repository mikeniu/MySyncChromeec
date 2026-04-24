/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _FT_HELP_H
#define _FT_HELP_H

const char ft_cmd_help_string[] = {
	"																	\n	\
If the MCU is currently in ROM mode or running a test FW with PID 0xface, please execute the following instructions for the upgrade.	\n	\
./focaltech_fingerprint_iap ec_full1201.bin 												\n	\
																	\n	\
If the MCU is currently running FW with PID 0x5403 or 0x5404, use the following command to force flash the firmware.			\n	\
./focaltech_fingerprint_iap ec_full1201.bin force_update_full_ec_bin									\n	\
																	\n	\
If the MCU is currently running FW with PID 0x5403 or 0x5404, please use the following command to enter bootloader mode.		\n	\
./focaltech_fingerprint_iap back_to_bootloader												\n	\
																	\n	\
If the MCU is in bootloader mode (VID 2808: PID 0001), it can only upgrade FW with focaltech hash verification.				\n	\
./focaltech_fingerprint_iap ec_hash_1201.bin												\n	\
																	\n	\
If the MCU is in bootloader mode (VID 2808: PID 0001), you can use the following command to revert to ROM mode.				\n	\
./focaltech_fingerprint_iap back_to_rom_boot												\n	\
																							\n  \
Use the following command to disable flash write protection. Note that it can only be executed in bootloader mode.			\n 	\
1 represents disabling write protection for all flash addresses.															\n 	\
2 represents disabling from the starting address up to the bootloader.														\n  \
3 represents disabling from the starting address up to RO.																	\n 	\
4 represents disabling from the starting address up to RW.																	\n 	\
./focaltech_fingerprint_iap 1 get_flash_protect																				\n 	\
"

};

#endif
