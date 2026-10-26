/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Flashing tool for RTS5817 (Realtek USB DFU device).
 *
 * Self-contained tool that:
 *   1. Parses VID:PID and firmware image path from command line
 *   2. Enables DFU download mode on the RTS5817 device via USB control
 *      transfers
 *   3. Performs DFU download directly via libusb (no external dfu-util)
 *
 * The DFU download protocol is implemented per the USB DFU Specification
 * (Interface Class 0xFE, SubClass 0x01).
 *
 * Dependencies: libusb-1.0 (compile-time, shared library at runtime)
 *
 * Usage:
 *   rts5817_flash -p VID:PID -i firmware.bin
 *
 * Example:
 *   rts5817_flash -p 0BDA:5817 -i firmware.bin
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "rts5817_flash_cli.h"
#include "rts5817_libusb_backend.h"
#include "rts5817_usb_device.h"

/* Default RTS5817 USB VID:PID */
constexpr uint16_t kRts5817Vid = 0x0BDA;
constexpr uint16_t kRts5817Pid = 0x5817;
constexpr uint16_t kRts5817DfuPid = 0x5800;

/* DFU_DETACH wValue timeout */
constexpr unsigned int kDetachTimeoutMs = 1000;
/* Max wait for the device to re-enumerate in DFU mode, and poll period */
constexpr unsigned int kReEnumTimeoutMs = 10000;
constexpr unsigned int kReEnumPollIntervalMs = 100;

int main(int argc, char **argv)
{
	Config cfg;
	uint16_t vid = kRts5817Vid;
	uint16_t pid = kRts5817Pid;

	if (!parse_args(argc, argv, cfg))
		return EXIT_FAILURE;

	/* Step 1: Read firmware into memory. */
	std::vector<uint8_t> firmware;
	if (!read_file(cfg.img, firmware)) {
		fprintf(stderr, "Failed to read firmware.\n");
		return EXIT_FAILURE;
	}
	printf("Firmware size: %zu bytes.\n", firmware.size());

	/* The device does not accept the DFU suffix as download payload. */
	size_t suffix_len = strip_dfu_suffix(firmware);
	if (suffix_len)
		printf("Stripped %zu-byte DFU suffix, payload is %zu bytes.\n",
		       suffix_len, firmware.size());

	/* Step 2: Parse VID:PID, falling back to the defaults. */
	if (!cfg.dev_id.empty()) {
		if (!parse_vid_pid(cfg.dev_id, vid, pid)) {
			fprintf(stderr,
				"Warning: could not parse '%s', using "
				"default %04x:%04x\n",
				cfg.dev_id.c_str(), vid, pid);
		}
	} else {
		printf("Using default VID:PID %04X:%04X\n", vid, pid);
	}

	/* Step 3: Open device and enable download mode. */
	RtsUsbDevice device(std::make_unique<RtsLibusbBackend>());
	if (!device.init(vid, pid))
		return EXIT_FAILURE;

	if (!device.enable_download())
		return EXIT_FAILURE;

	/*
	 * Step 4: Send DFU_DETACH to transition the device from Runtime mode
	 * to true DFU mode.  The device re-enumerates after this request.
	 */
	if (!device.dfu_detach(kDetachTimeoutMs))
		return EXIT_FAILURE;

	/* Step 5: Release the handle and poll for re-enumeration with the DFU
	 * PID.  Polling avoids the race and delay of a fixed sleep. */
	device.release();

	if (!device.wait_for_device(vid, kRts5817DfuPid, kReEnumTimeoutMs,
				    kReEnumPollIntervalMs))
		return EXIT_FAILURE;

	printf("Starting DFU download...\n");
	bool ok = device.dfu_download(firmware);

	if (ok) {
		printf("Flash completed successfully.\n");
		return EXIT_SUCCESS;
	}

	fprintf(stderr, "Flash failed.\n");
	return EXIT_FAILURE;
}
