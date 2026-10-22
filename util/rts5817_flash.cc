/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Flashing tool for RTS5817 (Realtek USB DFU device).
 *
 * Provides RtsUsbDevice, a thin libusb wrapper that opens the device,
 * enables download mode via vendor control requests, sends DFU_DETACH and
 * releases all USB resources.
 *
 * Dependencies: libusb-1.0 (compile-time, shared library at runtime)
 */

#include <cstdint>
#include <cstdio>

#include <libusb-1.0/libusb.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/* RTS5817 vendor-specific USB control requests (same as rtsflash.py) */
constexpr uint8_t kRtsStdGetStatus = 0x1A;
constexpr uint8_t kRtsStdSetStatus = 0x1B;

/* DFU class-specific requests (bmRequestType = 0x21 or 0xA1) */
constexpr uint8_t kDfuDetach = 0x00;

/* DFU descriptor type */
constexpr uint8_t kDfuDescriptorType = 0x21;

/* DFU class/subclass/protocol codes */
constexpr uint8_t kDfuClass = 0xFE;
constexpr uint8_t kDfuSubclass = 0x01;

/* Pre-computed bmRequestType values (raw uint8_t values per USB spec).
 * Avoids -Wdeprecated-enum-enum-conversion when OR'ing different libusb
 * enum types.
 *   0x80 = Device-to-host, Standard, Device
 *   0x00 = Host-to-device, Standard, Device
 *   0x21 = Host-to-device, Class, Interface
 *   0xA1 = Device-to-host, Class, Interface
 */
constexpr uint8_t kBmReqD2hStdDev = 0x80;
constexpr uint8_t kBmReqH2dStdDev = 0x00;
constexpr uint8_t kBmReqH2dClsIf = 0x21;

/* USB I/O timeouts */
constexpr unsigned int kUsbCtrlTimeoutMs = 1000;

/* Default transfer size if we cannot parse the DFU functional descriptor */
constexpr uint16_t kDfuDefaultTransferSize = 1024;

/* ---------------------------------------------------------------------------
 * RTS5817 USB Device + DFU Download
 * ------------------------------------------------------------------------- */

class RtsUsbDevice {
    public:
	RtsUsbDevice()
		: dev_handle_(nullptr)
		, ctx_(nullptr)
		, dfu_interface_(-1)
		, transfer_size_(kDfuDefaultTransferSize)
	{
	}
	~RtsUsbDevice()
	{
		release();
	}
	RtsUsbDevice(const RtsUsbDevice &) = delete;
	RtsUsbDevice &operator=(const RtsUsbDevice &) = delete;

	/** Open the device. */
	bool init(uint16_t vid, uint16_t pid);

	/**
	 * Poll every poll_interval_ms, up to timeout_ms, for the device to
	 * (re-)appear on the USB bus after a DFU_DETACH re-enumeration.
	 * Returns true once the device is opened, false on timeout.
	 */
	bool wait_for_device(uint16_t vid, uint16_t pid, int timeout_ms,
			     int poll_interval_ms);

	/** Release USB resources. */
	void release(void);

	/** Enable DFU download mode via vendor control request. */
	bool enable_download(void);

	/**
	 * Send DFU_DETACH to transition the device from Runtime mode to DFU
	 * mode.  After this call the device re-enumerates; close the handle,
	 * wait, and re-open before issuing DFU_DOWNLOAD.
	 *
	 * @detach_timeout_ms  Time the device waits before detaching (ms).
	 */
	bool dfu_detach(unsigned int detach_timeout_ms);

    private:
	/* Try to open the device without printing errors.  Requires a valid
	 * libusb context (see init()/wait_for_device()). */
	bool open_silent(uint16_t vid, uint16_t pid);

	/* Find the DFU interface and parse the DFU functional descriptor. */
	bool find_dfu_interface(void);

	libusb_device_handle *dev_handle_;
	libusb_context *ctx_;
	int dfu_interface_;
	uint16_t transfer_size_;
};

bool RtsUsbDevice::open_silent(uint16_t vid, uint16_t pid)
{
	dev_handle_ = libusb_open_device_with_vid_pid(ctx_, vid, pid);
	return dev_handle_ != nullptr;
}

bool RtsUsbDevice::init(uint16_t vid, uint16_t pid)
{
	int ret = libusb_init(&ctx_);
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			libusb_error_name(ret));
		return false;
	}

	if (!open_silent(vid, pid)) {
		fprintf(stderr,
			"Error: device %04x:%04x not found.\n"
			"Is the device connected and accessible?\n",
			vid, pid);
		libusb_exit(ctx_);
		ctx_ = nullptr;
		return false;
	}

	printf("Device %04x:%04x opened.\n", vid, pid);
	return true;
}

bool RtsUsbDevice::wait_for_device(uint16_t vid, uint16_t pid,
				   int timeout_ms, int poll_interval_ms)
{
	int ret = libusb_init(&ctx_);
	if (ret < 0) {
		fprintf(stderr, "Error: libusb_init() failed: %s\n",
			libusb_error_name(ret));
		return false;
	}

	int waited = 0;
	while (waited < timeout_ms) {
		if (open_silent(vid, pid)) {
			printf("Device %04x:%04x opened.\n", vid, pid);
			return true;
		}
		usleep(poll_interval_ms * 1000);
		waited += poll_interval_ms;
	}

	libusb_exit(ctx_);
	ctx_ = nullptr;
	fprintf(stderr, "Error: device %04x:%04x did not re-enumerate "
			"within %d ms.\n", vid, pid, timeout_ms);
	return false;
}

void RtsUsbDevice::release(void)
{
	if (dfu_interface_ >= 0 && dev_handle_) {
		libusb_release_interface(dev_handle_, dfu_interface_);
		dfu_interface_ = -1;
	}
	if (dev_handle_) {
		libusb_close(dev_handle_);
		dev_handle_ = nullptr;
	}
	if (ctx_) {
		libusb_exit(ctx_);
		ctx_ = nullptr;
	}
}

bool RtsUsbDevice::enable_download(void)
{
	if (!dev_handle_)
		return false;

	/*
	 * bmRequestType = 0x80: Device-to-host, Standard, Device
	 * bRequest = kRtsStdGetStatus (0x1A)
	 * wValue = 0, wIndex = 0
	 */
	uint8_t status = 0;
	int ret = libusb_control_transfer(dev_handle_, kBmReqD2hStdDev,
					  kRtsStdGetStatus, 0, 0, &status,
					  sizeof(status), kUsbCtrlTimeoutMs);

	if (ret < 0) {
		fprintf(stderr, "Error: GET_STATUS failed: %s\n",
			libusb_error_name(ret));
		return false;
	}

	if (status == 0) {
		/*
		 * bmRequestType = 0x00: Host-to-device, Standard, Device
		 * bRequest = kRtsStdSetStatus (0x1B)
		 * wValue = 1, wIndex = 0
		 */
		ret = libusb_control_transfer(dev_handle_, kBmReqH2dStdDev,
					      kRtsStdSetStatus, 1, 0, nullptr,
					      0, kUsbCtrlTimeoutMs);

		if (ret < 0) {
			fprintf(stderr, "Error: SET_STATUS failed: %s\n",
				libusb_error_name(ret));
			return false;
		}
		printf("Download mode enabled on device.\n");
	} else {
		printf("Download mode already active (status = %d).\n", status);
	}

	return true;
}

bool RtsUsbDevice::dfu_detach(unsigned int detach_timeout_ms)
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
	int ret = libusb_control_transfer(dev_handle_, kBmReqH2dClsIf,
					  kDfuDetach, detach_timeout_ms,
					  static_cast<uint16_t>(dfu_interface_),
					  nullptr, 0, kUsbCtrlTimeoutMs);

	if (ret < 0) {
		fprintf(stderr, "DFU_DETACH failed: %s\n",
			libusb_error_name(ret));
		return false;
	}

	printf("DFU_DETACH sent.  Waiting for re-enumeration...\n");
	return true;
}

bool RtsUsbDevice::find_dfu_interface(void)
{
	libusb_device *dev = libusb_get_device(dev_handle_);
	libusb_config_descriptor *config = nullptr;

	int ret = libusb_get_active_config_descriptor(dev, &config);
	if (ret < 0) {
		fprintf(stderr, "Error: cannot get config descriptor: %s\n",
			libusb_error_name(ret));
		return false;
	}

	dfu_interface_ = -1;
	transfer_size_ = kDfuDefaultTransferSize;

	/* Walk all interfaces and alternate settings. */
	for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
		for (int j = 0; j < config->interface[i].num_altsetting; j++) {
			const struct libusb_interface_descriptor *alt =
				&config->interface[i].altsetting[j];

			if (alt->bInterfaceClass != kDfuClass ||
			    alt->bInterfaceSubClass != kDfuSubclass)
				continue;

			dfu_interface_ = alt->bInterfaceNumber;

			/* Parse the DFU functional descriptor from extra
			 * bytes.  The descriptor has type 0x21 and contains
			 * wTransferSize at byte offset 5-6.
			 */
			int off = 0;
			while (off + 1 < alt->extra_length) {
				uint8_t desc_len = alt->extra[off];
				if (desc_len == 0)
					break;
				if (off + desc_len > alt->extra_length)
					break;
				if (alt->extra[off + 1] ==
					    kDfuDescriptorType &&
				    desc_len >= 9) {
					transfer_size_ =
						alt->extra[off + 5] |
						(alt->extra[off + 6] << 8);
					printf("DFU transfer size: %u bytes\n",
					       transfer_size_);
					break;
				}
				off += desc_len;
			}

			libusb_free_config_descriptor(config);
			printf("DFU interface found: #%d\n", dfu_interface_);
			return true;
		}
	}

	libusb_free_config_descriptor(config);

	if (dfu_interface_ < 0) {
		fprintf(stderr, "Error: no DFU interface found on device.\n");
		return false;
	}

	return true;
}
