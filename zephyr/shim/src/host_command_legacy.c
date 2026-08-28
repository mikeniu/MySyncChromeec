/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "task.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>

#ifndef CONFIG_ZTEST
#ifndef CONFIG_TASK_HOSTCMD_THREAD_MAIN
#error "The legacy Host Command subsystem is supported only with reusing " \
	"the main thread. Please enable CONFIG_TASK_HOSTCMD_THREAD_MAIN."
#endif
#endif /* !CONFIG_ZTEST */

BUILD_ASSERT(sizeof(struct host_cmd_handler_args) ==
		     sizeof(struct ec_host_cmd_handler_args),
	     "Incompatible structure.");
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, command) ==
	     offsetof(struct ec_host_cmd_handler_args, command));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, version) ==
	     offsetof(struct ec_host_cmd_handler_args, version));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, params) ==
	     offsetof(struct ec_host_cmd_handler_args, input_buf));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, params_size) ==
	     offsetof(struct ec_host_cmd_handler_args, input_buf_size));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response_max) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf_max));
BUILD_ASSERT(offsetof(struct host_cmd_handler_args, response_size) ==
	     offsetof(struct ec_host_cmd_handler_args, output_buf_size));

struct host_command *zephyr_find_host_command(int command)
{
	STRUCT_SECTION_FOREACH(host_command, cmd)
	{
		if (cmd->command == command)
			return cmd;
	}

	return NULL;
}

void host_command_main(void)
{
	k_thread_priority_set(get_main_thread(),
			      EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO));
	k_thread_name_set(get_main_thread(), "HOSTCMD");
	host_command_task(NULL);
}
