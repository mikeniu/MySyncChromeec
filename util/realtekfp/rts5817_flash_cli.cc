/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "rts5817_flash_cli.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fstream>
#include <getopt.h>

bool read_file(const std::string &path, std::vector<uint8_t> &data)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		fprintf(stderr, "Error: cannot open '%s' for reading: %s\n",
			path.c_str(), strerror(errno));
		return false;
	}

	std::streampos pos = file.tellg();
	if (pos < 0) {
		fprintf(stderr, "Error: cannot determine file size for '%s'\n",
			path.c_str());
		return false;
	}
	size_t size = static_cast<size_t>(pos);
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

/*
 * DFU suffix (USB DFU 1.1, 16 bytes): bcdDevice, idProduct, idVendor, bcdDFU,
 * ucDfuSignature ("UFD" in file order), bLength, dwCRC.
 */
static constexpr size_t kDfuSuffixLen = 16;
static constexpr size_t kDfuSuffixSigOff = 8;
static constexpr size_t kDfuSuffixLenOff = 11;
static constexpr uint8_t kDfuSuffixSig[] = { 'U', 'F', 'D' };

size_t strip_dfu_suffix(std::vector<uint8_t> &firmware)
{
	if (firmware.size() < kDfuSuffixLen)
		return 0;

	const uint8_t *suffix =
		firmware.data() + firmware.size() - kDfuSuffixLen;
	size_t len = suffix[kDfuSuffixLenOff];

	if (memcmp(suffix + kDfuSuffixSigOff, kDfuSuffixSig,
		   sizeof(kDfuSuffixSig)) != 0 ||
	    len < kDfuSuffixLen || len > firmware.size())
		return 0;

	firmware.resize(firmware.size() - len);
	return len;
}

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

bool parse_args(int argc, char **argv, Config &cfg)
{
	static const struct option long_opts[] = {
		{ "pid", required_argument, nullptr, 'p' },
		{ "img", required_argument, nullptr, 'i' },
		{ "help", no_argument, nullptr, 'h' },
		{ nullptr, 0, nullptr, 0 },
	};

	int opt;

	optind = 1;
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

	if (cfg.img.empty()) {
		fprintf(stderr, "Error: --img (-i) is required.\n");
		usage(argv[0]);
		return false;
	}

	return true;
}

bool parse_vid_pid(const std::string &dev_id, uint16_t &vid, uint16_t &pid)
{
	unsigned int v, p;
	int consumed = 0;

	if (sscanf(dev_id.c_str(), "%x:%x%n", &v, &p, &consumed) != 2)
		return false;

	if (static_cast<size_t>(consumed) != dev_id.length() || v > 0xFFFF ||
	    p > 0xFFFF)
		return false;

	vid = static_cast<uint16_t>(v);
	pid = static_cast<uint16_t>(p);
	return true;
}
