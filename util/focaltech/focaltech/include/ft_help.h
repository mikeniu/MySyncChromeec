#ifndef _FT_HELP_H
#define _FT_HELP_H
const char ft_cmd_help_string[]={
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
"
};







#endif
