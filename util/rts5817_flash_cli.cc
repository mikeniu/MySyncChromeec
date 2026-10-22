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

bool parse_vid_pid(const std::string &dev_id, uint16_t &vid, uint16_t &pid)
{
	unsigned int v, p;

	if (sscanf(dev_id.c_str(), "%x:%x", &v, &p) != 2)
		return false;

	vid = static_cast<uint16_t>(v);
	pid = static_cast<uint16_t>(p);
	return true;
}
