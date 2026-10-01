/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef PLATFORM_EC_UTIL_ECTOOL_H_
#define PLATFORM_EC_UTIL_ECTOOL_H_

/** @brief A handler for an `ectool` command.  */
struct command {
	/** The name of the command. */
	const char *name;

	/**
	 * The function to handle the command.
	 *
	 * @param argc The length of `argv`
	 * @param argv The arguments passed, including the command itself but
	 *             not 'ectool'.
	 * @return 0 if successful, or a negative `enum ec_status` value.
	 */
	int (*handler)(int argc, char *argv[]);

	/** The help text for this command, as
	 * "arguments\n\tShort description."
	 */
	const char *help;
};

/* ASCII mode for printing, default off */
extern int ascii_mode;

int cmd_i2c_read(int argc, char *argv[]);
int cmd_i2c_speed(int argc, char *argv[]);
int cmd_i2c_write(int argc, char *argv[]);
int cmd_i2c_xfer(int argc, char *argv[]);

#endif /* PLATFORM_EC_UTIL_ECTOOL_H_ */
