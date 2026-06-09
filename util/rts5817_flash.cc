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

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fstream>
#include <getopt.h>
#include <libusb-1.0/libusb.h>
#include <string>
#include <unistd.h>
#include <vector>

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

/* Default RTS5817 USB VID:PID */
constexpr uint16_t kRts5817Vid = 0x0BDA;
constexpr uint16_t kRts5817Pid = 0x5817;
constexpr uint16_t kRts5817DfuPid = 0x5800;

/* RTS5817 vendor-specific USB control requests (same as rtsflash.py) */
constexpr uint8_t kRtsStdGetStatus = 0x1A;
constexpr uint8_t kRtsStdSetStatus = 0x1B;

/* DFU class-specific requests (bmRequestType = 0x21 or 0xA1) */
constexpr uint8_t kDfuDetach = 0x00;
constexpr uint8_t kDfuDownload = 0x01;
constexpr uint8_t kDfuGetStatus = 0x03;
constexpr uint8_t kDfuClrStatus = 0x04;

/* DFU states (from USB DFU spec) */
constexpr uint8_t kDfuStateDfuIdle = 2;
constexpr uint8_t kDfuStateDfuDnloadIdle = 5;
constexpr uint8_t kDfuStateDfuManifestWaitReset = 8;
constexpr uint8_t kDfuStateDfuError = 10;

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
constexpr uint8_t kBmReqD2hClsIf = 0xA1;

/* USB I/O timeouts */
constexpr unsigned int kUsbCtrlTimeoutMs = 1000;
constexpr unsigned int kUsbDfuTimeoutMs = 60000;

/* Default transfer size if we cannot parse the DFU functional descriptor */
constexpr uint16_t kDfuDefaultTransferSize = 1024;

/**
 * read_file - Read an entire binary file into a vector.
 * @path:  File path.
 * @data:  Output buffer.
 * Return: true on success, false on error.
 */
static bool read_file(const std::string &path, std::vector<uint8_t> &data)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		fprintf(stderr, "Error: cannot open '%s' for reading: %s\n",
			path.c_str(), strerror(errno));
		return false;
	}

	size_t size = static_cast<size_t>(file.tellg());
	file.seekg(0, std::ios::beg);

	data.resize(size);
	file.read(reinterpret_cast<char *>(data.data()),
		  static_cast<std::streamsize>(size));

	if (!file) {
		fprintf(stderr, "Error: short read from '%s'\n", path.c_str());
		return false;
	}
	return true;
}

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
	 * Perform the full DFU firmware download.
	 * Must be called after init() and enable_download().
	 * @firmware:  Complete firmware blob (including DFU suffix).
	 */
	bool dfu_download(const std::vector<uint8_t> &firmware);

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

	/**
	 * Poll DFU_GETSTATUS until the device is not busy.
	 * Return: true if device reaches dfuIDLE or dfuDNLOAD_IDLE.
	 */
	bool dfu_poll_idle(void);

	/**
	 * Clear DFU error status (DFU_CLRSTATUS).
	 */
	bool dfu_clear_status(void);

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

bool RtsUsbDevice::dfu_poll_idle(void)
{
	/*
	 * Use a generous timeout (60 s) for the overall polling, since the
	 * manifest phase (flash programming) can take tens of seconds.
	 */
	int timeout = 60000;
	int slept = 0;

	while (slept < timeout) {
		/*
		 * bmRequestType = 0xA1: Device-to-Host, Class, Interface
		 * bRequest = kDfuGetStatus (0x03)
		 * Data (6 bytes): bStatus, bwPollTimeout[3], bState, iString
		 */
		uint8_t response[6];
		int ret = libusb_control_transfer(
			dev_handle_, kBmReqD2hClsIf, kDfuGetStatus, 0,
			static_cast<uint16_t>(dfu_interface_), response,
			sizeof(response), kUsbDfuTimeoutMs);

		if (ret < 6) {
			fprintf(stderr, "DFU_GETSTATUS failed: %s\n",
				ret < 0 ? libusb_error_name(ret) : "short read");
			return false;
		}

		uint8_t bStatus = response[0];
		uint32_t poll_timeout = response[1] | (response[2] << 8) |
					(response[3] << 16);
		uint8_t bState = response[4];

		/* Transition to sleep after processing the state. */
		int delay_ms = (poll_timeout > 0) ? (int)poll_timeout : 1;

		switch (bState) {
		case kDfuStateDfuIdle:
		case kDfuStateDfuDnloadIdle:
			/* Terminal success states. */
			return true;

		case kDfuStateDfuError:
			fprintf(stderr, "DFU error: bStatus=%d, bState=%d\n",
				bStatus, bState);
			return false;

		case kDfuStateDfuManifestWaitReset:
			/*
			 * Device completed manifest and requests a USB reset.
			 * Try issuing one; this may invalidate the device
			 * handle (if the device re-enumerates), which is OK.
			 */
			printf("Device requests USB reset after manifest.\n");
			libusb_reset_device(dev_handle_);
			return true;

		default:
			/*
			 * Transient / busy states:
			 *   DFU_DNLOAD_SYNC  (3)  — download request received
			 *   DFU_DNLOAD_BUSY  (4)  — programming memory
			 *   DFU_MANIFEST_SYNC (6) — ZLP received, enter
			 * manifest DFU_MANIFEST      (7) — programming
			 * completed, waiting for reset / done
			 *
			 * Sleep for the suggested poll interval and retry.
			 */
			break;
		}

		usleep(delay_ms * 1000);
		slept += delay_ms;
	}

	fprintf(stderr, "Timed out waiting for DFU idle state.\n");
	return false;
}

bool RtsUsbDevice::dfu_clear_status(void)
{
	/*
	 * bmRequestType = 0x21: Host-to-Device, Class, Interface
	 * bRequest = kDfuClrStatus (0x04)
	 */
	int ret = libusb_control_transfer(dev_handle_, kBmReqH2dClsIf,
					  kDfuClrStatus, 0,
					  static_cast<uint16_t>(dfu_interface_),
					  nullptr, 0, kUsbCtrlTimeoutMs);

	if (ret < 0) {
		fprintf(stderr, "DFU_CLRSTATUS failed: %s\n",
			libusb_error_name(ret));
		return false;
	}
	return true;
}

bool RtsUsbDevice::dfu_download(const std::vector<uint8_t> &firmware)
{
	if (!dev_handle_)
		return false;

	/* Step 1: Find the DFU interface. */
	if (!find_dfu_interface())
		return false;

	/* Detach the kernel driver from the DFU interface, if any. */
	if (libusb_kernel_driver_active(dev_handle_, dfu_interface_) == 1) {
		int kd_ret = libusb_detach_kernel_driver(dev_handle_,
							    dfu_interface_);
		if (kd_ret < 0) {
			fprintf(stderr,
				"Warning: cannot detach kernel driver: %s\n",
				libusb_error_name(kd_ret));
		}
	}

	int ret = libusb_claim_interface(dev_handle_, dfu_interface_);
	if (ret < 0) {
		fprintf(stderr, "Error: cannot claim DFU interface: %s\n",
			libusb_error_name(ret));
		return false;
	}

	/* Step 2: Send firmware in blocks using DFU_DOWNLOAD. */
	const size_t total_size = firmware.size();
	size_t offset = 0;
	uint16_t block_num = 0;
	uint64_t printed_pct = 0;

	printf("Flashing %zu bytes (%u-byte blocks)...\n", total_size,
	       transfer_size_);

	while (offset < total_size) {
		size_t chunk_size = total_size - offset;
		if (chunk_size > transfer_size_)
			chunk_size = transfer_size_;

		/*
		 * bmRequestType = 0x21: Host-to-Device, Class, Interface
		 * bRequest = kDfuDownload (0x01)
		 * wValue  = block_num
		 * wIndex  = interface number
		 */
		ret = libusb_control_transfer(
			dev_handle_, kBmReqH2dClsIf, kDfuDownload, block_num,
			static_cast<uint16_t>(dfu_interface_),
			const_cast<uint8_t *>(firmware.data() + offset),
			static_cast<uint16_t>(chunk_size), kUsbDfuTimeoutMs);

		if (ret < 0) {
			fprintf(stderr,
				"\nDFU_DOWNLOAD failed at block %u "
				"(offset 0x%zx): %s\n",
				block_num, offset, libusb_error_name(ret));

			/* Try to clear error and continue. */
			dfu_clear_status();
			return false;
		}

		/* Poll status to wait for block to be processed. */
		if (!dfu_poll_idle()) {
			fprintf(stderr,
				"DFU status error after block %u "
				"(offset 0x%zx).\n",
				block_num, offset);
			return false;
		}

		block_num++;
		offset += chunk_size;

		/* Print progress every 10%. */
		uint64_t pct = (uint64_t)offset * 100 / total_size;
		if (pct >= printed_pct + 10) {
			printf("\r  Progress: %zu%%", (size_t)pct);
			fflush(stdout);
			printed_pct = pct;
		}
	}

	/* Step 3: Send zero-length packet to signal completion. */
	ret = libusb_control_transfer(dev_handle_, kBmReqH2dClsIf,
				      kDfuDownload, block_num,
				      static_cast<uint16_t>(dfu_interface_),
				      nullptr, 0, kUsbDfuTimeoutMs);

	if (ret < 0) {
		fprintf(stderr,
			"\nWarning: final DFU_DOWNLOAD (ZLP) failed: %s\n",
			libusb_error_name(ret));
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

	libusb_release_interface(dev_handle_, dfu_interface_);
	dfu_interface_ = -1;

	return true;
}

/* ---------------------------------------------------------------------------
 * Argument parsing
 * ------------------------------------------------------------------------- */

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s -p VID:PID -i firmware.bin\n"
		"\n"
		"Flash firmware (via DFU) to an RTS5817 USB device.\n"
		"\n"
		"Options:\n"
		"  -p, --pid   VID:PID of the target device (e.g., 0BDA:5817)\n"
		"  -i, --img   Path to the firmware binary\n"
		"  -h, --help  Show this help\n"
		"\n"
		"Example:\n"
		"  %s -p 0BDA:5817 -i firmware.bin\n",
		prog, prog);
}

struct Config {
	std::string dev_id; /* "VID:PID" string */
	std::string img; /* firmware image path */
};

static bool parse_args(int argc, char **argv, Config& cfg)
{
	static const struct option long_opts[] = {
		{ "pid", required_argument, nullptr, 'p' },
		{ "img", required_argument, nullptr, 'i' },
		{ "help", no_argument, nullptr, 'h' },
		{ nullptr, 0, nullptr, 0 },
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "p:i:h", long_opts, nullptr)) !=
	       -1) {
		switch (opt) {
		case 'p':
			cfg.dev_id = optarg;
			break;
		case 'i':
			cfg.img = optarg;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return false;
		}
	}

	if (cfg.dev_id.empty()) {
		cfg.dev_id = "0BDA:5817";
		printf("Using default VID:PID %s\n", cfg.dev_id.c_str());
	}

	if (cfg.img.empty()) {
		fprintf(stderr, "Error: --img (-i) is required.\n");
		usage(argv[0]);
		return false;
	}

	return true;
}

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

	/* Step 2: Parse VID:PID. */
	unsigned int v, p;
	if (sscanf(cfg.dev_id.c_str(), "%x:%x", &v, &p) == 2) {
		vid = static_cast<uint16_t>(v);
		pid = static_cast<uint16_t>(p);
	} else {
		fprintf(stderr,
			"Warning: could not parse '%s', using "
			"default %04x:%04x\n",
			cfg.dev_id.c_str(), vid, pid);
	}

	/* Step 3: Open device and enable download mode. */
	RtsUsbDevice device;
	if (!device.init(vid, pid))
		return EXIT_FAILURE;

	if (!device.enable_download()) {
		device.release();
		return EXIT_FAILURE;
	}

	/*
	 * Step 4: Send DFU_DETACH to transition the device from Runtime mode
	 * to true DFU mode.  The device re-enumerates after this request.
	 */
	if (!device.dfu_detach(1000)) {
		device.release();
		return EXIT_FAILURE;
	}

	/* Step 5: Release the handle and poll for re-enumeration with the DFU
	 * PID.  Polling avoids the race and delay of a fixed sleep. */
	device.release();

	if (!device.wait_for_device(vid, kRts5817DfuPid, 10000, 100))
		return EXIT_FAILURE;

	printf("Starting DFU download...\n");
	bool ok = device.dfu_download(firmware);

	device.release();

	if (ok) {
		printf("Flash completed successfully.\n");
		return EXIT_SUCCESS;
	}

	fprintf(stderr, "Flash failed.\n");
	return EXIT_FAILURE;
}
