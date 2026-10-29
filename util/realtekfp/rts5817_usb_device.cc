/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_usb_device.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

void RtsUsbDevice::default_sleep_ms(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

RtsUsbDevice::RtsUsbDevice(std::unique_ptr<RtsUsbBackend> backend,
			   SleepFn sleep_ms)
	: backend_(std::move(backend)), sleep_ms_(std::move(sleep_ms))
{
}

RtsUsbDevice::~RtsUsbDevice()
{
	release();
}

bool RtsUsbDevice::init(uint16_t vid, uint16_t pid)
{
	release();

	int ret = backend_->init();
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			backend_->error_name(ret));
		return false;
	}

	if (!backend_->open(vid, pid)) {
		fprintf(stderr,
			"Error: device %04x:%04x not found.\n"
			"Is the device connected and accessible?\n",
			vid, pid);
		release();
		return false;
	}

	printf("Device %04x:%04x opened.\n", vid, pid);
	return true;
}

bool RtsUsbDevice::wait_for_device(uint16_t vid, uint16_t pid, int timeout_ms,
				   int poll_interval_ms)
{
	if (timeout_ms <= 0 || poll_interval_ms <= 0)
		return false;

	release();

	int ret = backend_->init();
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			backend_->error_name(ret));
		return false;
	}

	for (int waited = 0; waited < timeout_ms; waited += poll_interval_ms) {
		if (backend_->open(vid, pid)) {
			printf("Device %04x:%04x opened.\n", vid, pid);
			return true;
		}
		sleep_ms_(poll_interval_ms);
	}

	release();
	fprintf(stderr,
		"Error: device %04x:%04x did not re-enumerate within %d ms.\n",
		vid, pid, timeout_ms);
	return false;
}

void RtsUsbDevice::release()
{
	dfu_interface_ = -1;
	transfer_size_ = kDfuDefaultTransferSize;
	backend_->close();
}

bool RtsUsbDevice::enable_download()
{
	if (!backend_->is_open())
		return false;

	/*
	 * bmRequestType = 0x80: Device-to-host, Standard, Device
	 * bRequest = kRtsStdGetStatus (0x1A)
	 * wValue = 0, wIndex = 0
	 */
	uint8_t status = 0;
	int ret = backend_->control_transfer(
		kBmReqD2hStdDev, kRtsStdGetStatus, 0, 0, &status, sizeof(status),
		kUsbCtrlTimeoutMs);

	if (ret < static_cast<int>(sizeof(status))) {
		fprintf(stderr, "Error: GET_STATUS failed: %s\n",
			ret < 0 ? backend_->error_name(ret) : "short read");
		return false;
	}

	if (status == 0) {
		/*
		 * bmRequestType = 0x00: Host-to-device, Standard, Device
		 * bRequest = kRtsStdSetStatus (0x1B)
		 * wValue = kRtsEnableDownloadMode, wIndex = 0
		 */
		ret = backend_->control_transfer(
			kBmReqH2dStdDev, kRtsStdSetStatus, kRtsEnableDownloadMode, 0,
			nullptr, 0, kUsbCtrlTimeoutMs);

		if (ret < 0) {
			fprintf(stderr, "Error: SET_STATUS failed: %s\n",
				backend_->error_name(ret));
			return false;
		}
		printf("Download mode enabled on device.\n");
	} else {
		printf("Download mode already active (status = %d).\n", status);
	}

	return true;
}

bool RtsUsbDevice::dfu_detach(uint16_t detach_timeout_ms)
{
	/*
	 * DFU_DETACH tells the device to switch from Runtime mode to DFU
	 * mode.  The device responds by re-enumerating on the USB bus.
	 */

	/* Find the DFU interface first. */
	if (!find_dfu_interface())
		return false;

	/*
	 * bmRequestType = 0x21: Host-to-Device, Class, Interface
	 * bRequest = kDfuDetach (0x00)
	 * wValue = detach_timeout_ms
	 * wIndex = interface number
	 */
	int ret = backend_->control_transfer(
		kBmReqH2dClsIf, kDfuDetach, detach_timeout_ms,
		static_cast<uint16_t>(dfu_interface_), nullptr, 0,
		kUsbCtrlTimeoutMs);

	if (ret < 0) {
		fprintf(stderr, "DFU_DETACH failed: %s\n",
			backend_->error_name(ret));
		return false;
	}

	printf("DFU_DETACH sent.  Waiting for re-enumeration...\n");
	return true;
}

uint16_t parse_dfu_transfer_size(std::span<const uint8_t> extra)
{
	/*
	 * Walk the descriptor stream.  The DFU functional descriptor has type
	 * 0x21 and contains wTransferSize at byte offset 5-6.
	 */
	while (extra.size() > kDfuDescTypeOff) {
		uint8_t desc_len = extra[0];

		if (desc_len == 0 || desc_len > extra.size())
			break;

		if (extra[kDfuDescTypeOff] == kDfuDescriptorType &&
		    desc_len >= kDfuDescMinLen) {
			uint16_t size = extra[kDfuDescXferSizeOff] |
					(extra[kDfuDescXferSizeOff + 1] << 8);

			return size != 0 ? size : kDfuDefaultTransferSize;
		}
		extra = extra.subspan(desc_len);
	}

	return kDfuDefaultTransferSize;
}

bool RtsUsbDevice::find_dfu_interface()
{
	if (!backend_->is_open())
		return false;

	std::vector<RtsUsbInterface> interfaces;
	int ret = backend_->get_interfaces(&interfaces);

	if (ret < 0) {
		fprintf(stderr, "Error: cannot get config descriptor: %s\n",
			backend_->error_name(ret));
		return false;
	}

	dfu_interface_ = -1;
	transfer_size_ = kDfuDefaultTransferSize;

	/* Walk all interfaces and alternate settings. */
	for (const RtsUsbInterface &alt : interfaces) {
		if (alt.usb_class != kDfuClass ||
		    alt.usb_subclass != kDfuSubclass)
			continue;

		dfu_interface_ = alt.number;
		transfer_size_ = parse_dfu_transfer_size(alt.extra);
		printf("DFU transfer size: %u bytes\n", transfer_size_);
		printf("DFU interface found: #%d\n", dfu_interface_);
		return true;
	}

	fprintf(stderr, "Error: no DFU interface found on device.\n");
	return false;
}

bool RtsUsbDevice::dfu_poll_idle()
{
	/*
	 * Use a generous timeout (60 s) for the overall polling, since the
	 * manifest phase (flash programming) can take tens of seconds.
	 */
	int timeout = kUsbDfuTimeoutMs;
	int slept = 0;

	while (slept < timeout) {
		/*
		 * bmRequestType = 0xA1: Device-to-Host, Class, Interface
		 * bRequest = kDfuGetStatus (0x03)
		 * Data (6 bytes): bStatus, bwPollTimeout[3], bState, iString
		 */
		uint8_t response[kDfuGetStatusResponseLen];
		int ret = backend_->control_transfer(
			kBmReqD2hClsIf, kDfuGetStatus, 0,
			static_cast<uint16_t>(dfu_interface_), response,
			sizeof(response), kUsbDfuTimeoutMs);

		if (ret < static_cast<int>(kDfuGetStatusResponseLen)) {
			fprintf(stderr, "DFU_GETSTATUS failed: %s\n",
				ret < 0 ? backend_->error_name(ret) :
					  "short read");
			return false;
		}

		uint8_t bStatus = response[kDfuStatusOff];
		uint32_t poll_timeout =
			response[kDfuPollTimeoutOff] |
			(static_cast<uint32_t>(response[kDfuPollTimeoutOff + 1])
			 << 8) |
			(static_cast<uint32_t>(response[kDfuPollTimeoutOff + 2])
			 << 16);
		uint8_t bState = response[kDfuStateOff];

		if (bStatus != kDfuStatusOk || bState == kDfuStateDfuError) {
			fprintf(stderr, "DFU error: bStatus=%d, bState=%d\n",
				bStatus, bState);
			return false;
		}

		switch (bState) {
		case kDfuStateDfuIdle:
		case kDfuStateDfuDnloadIdle:
			/* Terminal success states. */
			return true;

		case kDfuStateDfuManifestWaitReset:
			/*
			 * Device completed manifest and requests a USB reset.
			 * Try issuing one; this may invalidate the device
			 * handle (if the device re-enumerates), which is OK.
			 */
			printf("Device requests USB reset after manifest.\n");
			backend_->reset_device();
			return true;

		case kDfuStateDfuDnloadSync:
		case kDfuStateDfuDnloadBusy:
		case kDfuStateDfuManifestSync:
		case kDfuStateDfuManifest:
			/*
			 * Transient / busy states: sleep for the suggested
			 * poll interval and retry.
			 */
			break;

		default:
			fprintf(stderr, "Unexpected DFU state: bState=%d\n",
				bState);
			return false;
		}

		/* Never sleep past the overall timeout. */
		int remaining_ms = timeout - slept;
		int delay_ms = (poll_timeout > 0) ?
				       static_cast<int>(std::min<uint32_t>(
					       poll_timeout, remaining_ms)) :
				       std::min(kDfuMinPollDelayMs,
						remaining_ms);

		sleep_ms_(delay_ms);
		slept += delay_ms;
	}

	fprintf(stderr, "Timed out waiting for DFU idle state.\n");
	return false;
}

bool RtsUsbDevice::dfu_clear_status()
{
	/*
	 * bmRequestType = 0x21: Host-to-Device, Class, Interface
	 * bRequest = kDfuClrStatus (0x04)
	 */
	int ret = backend_->control_transfer(
		kBmReqH2dClsIf, kDfuClrStatus, 0,
		static_cast<uint16_t>(dfu_interface_), nullptr, 0,
		kUsbCtrlTimeoutMs);

	if (ret < 0) {
		fprintf(stderr, "DFU_CLRSTATUS failed: %s\n",
			backend_->error_name(ret));
		return false;
	}
	return true;
}

bool RtsUsbDevice::dfu_download(const std::vector<uint8_t> &firmware)
{
	/* Step 1: Find the DFU interface. */
	if (firmware.empty() || !find_dfu_interface())
		return false;

	/* Detach the kernel driver from the DFU interface, if any. */
	int kd_ret = backend_->detach_kernel_driver(dfu_interface_);
	if (kd_ret < 0) {
		fprintf(stderr, "Warning: cannot detach kernel driver: %s\n",
			backend_->error_name(kd_ret));
	}

	int ret = backend_->claim_interface(dfu_interface_);
	if (ret < 0) {
		fprintf(stderr, "Error: cannot claim DFU interface: %s\n",
			backend_->error_name(ret));
		dfu_interface_ = -1;
		return false;
	}

	/* Step 2: Send firmware in blocks using DFU_DOWNLOAD. */
	const size_t total_size = firmware.size();
	size_t offset = 0;
	uint16_t block_num = 0;
	size_t printed_pct = 0;

	printf("Flashing %zu bytes (%u-byte blocks)...\n", total_size,
	       transfer_size_);

	while (offset < total_size) {
		size_t chunk_size =
			std::min(total_size - offset,
				 static_cast<size_t>(transfer_size_));

		/*
		 * bmRequestType = 0x21: Host-to-Device, Class, Interface
		 * bRequest = kDfuDownload (0x01)
		 * wValue  = block_num
		 * wIndex  = interface number
		 */
		ret = backend_->control_transfer(
			kBmReqH2dClsIf, kDfuDownload, block_num,
			static_cast<uint16_t>(dfu_interface_),
			const_cast<uint8_t *>(firmware.data() + offset),
			static_cast<uint16_t>(chunk_size), kUsbDfuTimeoutMs);

		if (ret != static_cast<int>(chunk_size)) {
			fprintf(stderr,
				"\nDFU_DOWNLOAD failed at block %u "
				"(offset 0x%zx): %s\n",
				block_num, offset,
				ret < 0 ? backend_->error_name(ret) :
					  "short write");

			/* Try to clear error and abort. */
			dfu_clear_status();
			backend_->release_interface(dfu_interface_);
			dfu_interface_ = -1;
			return false;
		}

		/* Poll status to wait for block to be processed. */
		if (!dfu_poll_idle()) {
			fprintf(stderr,
				"DFU status error after block %u "
				"(offset 0x%zx).\n",
				block_num, offset);
			backend_->release_interface(dfu_interface_);
			dfu_interface_ = -1;
			return false;
		}

		block_num++;
		offset += chunk_size;

		/* Print progress every 10%. */
		size_t pct = offset * 100 / total_size;
		if (pct >= printed_pct + kDfuProgressStepPct) {
			printf("\r  Progress: %zu%%", pct);
			fflush(stdout);
			printed_pct = pct;
		}
	}

	/* Step 3: Send zero-length packet to signal completion. */
	ret = backend_->control_transfer(kBmReqH2dClsIf,
				      kDfuDownload, block_num,
				      static_cast<uint16_t>(dfu_interface_),
				      nullptr, 0, kUsbDfuTimeoutMs);

	if (ret < 0) {
		fprintf(stderr,
			"\nWarning: final DFU_DOWNLOAD (ZLP) failed: %s\n",
			backend_->error_name(ret));
		/* Device may still complete the operation; try polling. */
	}

	/* Step 4: Wait for the device to finish (MANIFEST -> IDLE). */
	printf("\r  Progress: 100%%\n");
	printf("Waiting for device to complete...\n");

	/*
	 * After the final ZLP, the device goes through manifest phase.
	 * Poll GETSTATUS until it returns to dfuIDLE.
	 */
	if (!dfu_poll_idle()) {
		fprintf(stderr, "Warning: device did not reach idle after "
				"manifest.\n");
		/* This may still be OK; some devices self-reset here. */
	}

	backend_->release_interface(dfu_interface_);
	dfu_interface_ = -1;

	return true;
}
