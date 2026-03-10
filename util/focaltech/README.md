how to build this code:

1.To compile the ALOS tool, first download the Android NDK from the following link:
https://dl.google.com/android/repository/android-ndk-r27d-linux.zip

2.Set up the environment variables.
```bash
export PATH=$PATH:/home/rd/android-ndk-r27d-linux/android-ndk-r27d
```

3.Under the jni directory, use ndk-build to begin compilation.

4.To compile the universal Linux tool, please execute make under the jni directory.

If the MCU is currently in ROM mode or running a test FW with PID 0xface, please execute the following instructions for the upgrade.
```bash
./focaltech_fingerprint_iap ec_full1201.bin 
```

If the MCU is currently running FW with PID 0x5403 or 0x5404, use the following command to force flash the firmware.
```bash
./focaltech_fingerprint_iap ec_full1201.bin force_update_full_ec_bin
```

If the MCU is currently running FW with PID 0x5403 or 0x5404, please use the following command to enter bootloader mode.
```bash
./focaltech_fingerprint_iap back_to_bootloader
```

If the MCU is in bootloader mode (VID 2808: PID 0001), it can only upgrade FW with focaltech hash verification.
```bash
sudo ./focaltech_fingerprint_iap_linux ../ec_hash_1201.bin
```

If the MCU is in bootloader mode (VID 2808: PID 0001), you can use the following command to revert to ROM mode.
```bash
./focaltech_fingerprint_iap back_to_rom_boot
```

The following is a partial log of the successful upgrade.

```bash
I [focaltech] find device pid 5403:vid 18d1
D [focaltech] free device list
D [focaltech] close the device
D [focaltech] connect the device successfully.
D [focaltech] claim the device interface successfully.
D [focaltech] ep out=1
D [focaltech] ep in=81
I [focaltech] ec build version: chojnik-0.0.0-01e3566+ 2025-12-01 15:55:28 rd@WUJIE-Series
D [focaltech] force update ec bin
D [focaltech] ft_switch_to_boot enter
D [focaltech] ep out=1
D [focaltech] send ret=8
D [focaltech] ep in=81
D [focaltech] ret=fffffff8,recv 0,0,0,0
D [focaltech] ep out=1
D [focaltech] send ret=8
D [focaltech] ep in=81
D [focaltech] ret=ffffffff,recv 0,0,0,0
W [focaltech] mcu reset ,can't recv data from usb device
D [focaltech] release the device.
I [focaltech] begin_down binfile:ec_full1201.bin
D [focaltech] Device opened: 2808:0001
D [focaltech]   bDeviceClass: 0x00
D [focaltech]   Interface 0: bInterfaceClass=0x08, bInterfaceSubClass=0x06
D [focaltech] Detaching kernel driver
I [focaltech] get boot vesion:Bootloader Version 1.8
D [focaltech] force back to rom boot inner
D [focaltech] already has config,rst mcu
D [focaltech] ret value=0
D [focaltech] Device opened: 2fd0:0000
D [focaltech]   bDeviceClass: 0x00
D [focaltech]   Interface 0: bInterfaceClass=0x08, bInterfaceSubClass=0x06
D [focaltech] Detaching kernel driver
I [focaltech] get boot vesion:CC Bootloader Version 1.0:2020-12-01
D [focaltech] code_valid_control_word=3aec3721
D [focaltech] config_page_verification_enable=ffffffff
D [focaltech] code_start_address=10002000
D [focaltech] code_length=ffffffff
D [focaltech] config_page_location=ffffffff
D [focaltech] config_page_hash=66,99,38,c0
I [focaltech] should write cfg page last
D [focaltech] ===download 1/512 ===
D [focaltech] ===download 2/512 ===
D ......
D [focaltech] ===download 511/512 ===
I [focaltech] write cfg page start
D [focaltech] ===download 512/512 ===
D [focaltech] file len=2097152
I [focaltech] Down success!
D [focaltech] already has config,rst mcu
D [focaltech] ret value=0
I [focaltech] Disboot success!how to build this code:
```
