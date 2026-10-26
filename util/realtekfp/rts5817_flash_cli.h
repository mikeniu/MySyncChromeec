/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Command line and file helpers for the RTS5817 flashing tool.
 */

#ifndef PLATFORM_EC_UTIL_RTS5817_FLASH_CLI_H_
#define PLATFORM_EC_UTIL_RTS5817_FLASH_CLI_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Config {
	std::string dev_id; /* "VID:PID" string, empty if not given */
	std::string img; /* firmware image path */
};

/**
 * read_file - Read an entire binary file into a vector.
 * @path:  File path.
 * @data:  Output buffer.
 * Return: true on success, false on error.
 */
bool read_file(const std::string &path, std::vector<uint8_t> &data);

/**
 * strip_dfu_suffix - Remove a trailing DFU suffix from a firmware image.
 * @firmware:  Image to modify in place.
 * The suffix is only removed when it ends in a valid "UFD" signature and a
 * plausible bLength; otherwise the image is left untouched.
 * Return: number of bytes removed (0 if no suffix was found).
 */
size_t strip_dfu_suffix(std::vector<uint8_t> &firmware);

/**
 * parse_args - Parse command line options into @cfg.
 * Resets getopt state, so it can be called more than once.
 * Return: true if the tool should proceed, false on error or --help.
 */
bool parse_args(int argc, char **argv, Config &cfg);

/**
 * parse_vid_pid - Parse a "VID:PID" string (hexadecimal).
 * @vid, @pid are only modified on success.
 * Return: true on success, false if the string is malformed.
 */
bool parse_vid_pid(const std::string &dev_id, uint16_t &vid, uint16_t &pid);

#endif /* PLATFORM_EC_UTIL_RTS5817_FLASH_CLI_H_ */
