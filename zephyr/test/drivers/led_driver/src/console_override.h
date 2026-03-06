/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Console command signature compatibility header
 * This header provides a workaround for the signature mismatch between
 * the driver's console command function and the DECLARE_CONSOLE_COMMAND
 * macro. The driver function has: int console_command_max695x_write
 * (int argc, char **argv). But DECLARE_CONSOLE_COMMAND expects:
 * int (*)(int argc, const char **argv).
 * Override the macro to create a wrapper that handles the signature
 * mismatch.
 */

#ifndef CONSOLE_OVERRIDE_H_
#define CONSOLE_OVERRIDE_H_
/* Include the original zephyr console shim header first */
#include "zephyr_console_shim.h"

/* Save the original macro definition */
#ifdef DECLARE_CONSOLE_COMMAND
#undef DECLARE_CONSOLE_COMMAND
#endif

/*
 * Define a test-friendly version that creates a wrapper for signature
 * mismatch
 */
#define DECLARE_CONSOLE_COMMAND(NAME, ROUTINE, ARGDESC, HELP)                  \
	/* Create wrapper function to handle signature mismatch */             \
	static int _CONCAT(wrapper_, ROUTINE)(int argc, const char **argv)     \
	{                                                                      \
		/* Cast const char** to char** for old signature compatibility \
		 */                                                            \
		return ROUTINE(argc, (char **)argv);                           \
	}                                                                      \
	/* Use the wrapper in the Zephyr shell command registration */         \
	_ZEPHYR_SHELL_COMMAND_SHIM(NAME, _CONCAT(wrapper_, ROUTINE), ARGDESC,  \
				   HELP)

#endif /* CONSOLE_OVERRIDE_H_ */
