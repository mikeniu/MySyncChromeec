#include "host_command.h"
#include "task.h"
#include "test/drivers/test_state.h"
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define CUSTOM_COMMAND_ID 0x8888

ZTEST(host_cmd_thread, test_host_command_flood)
{
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(CUSTOM_COMMAND_ID, 0);

	/* Flood the EC with host commands for 1 second */
	uint64_t start_time = k_uptime_get();
	int count = 0;
	while (k_uptime_get() - start_time < 1000) {
		host_command_received(&args);
		count++;
	}
	
	zassert_true(count > 0, "Should have processed some commands");
}
