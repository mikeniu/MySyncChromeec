<<<<<<< HEAD   (06d697de6146d1743e05fead4671d2e94950f195 Revert "common: host_command: Reject overlapping commands wi)
||||||| BASE   (2a0ae872653c2d6d84672ff58e8678322b2ea225 hekla: Change GPIO definition for next phase)
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(battery_config, test_get)
{
	uint8_t buf[BATT_CONF_MAX_SIZE];
	struct host_cmd_handler_args args;
	struct batt_conf_header *head;
	struct board_batt_params conf;
	uint8_t *p = buf;
	int expected;

	zassert_ok(ec_cmd_battery_config(&args, buf));

	/* Verify metadata. */
	head = (struct batt_conf_header *)buf;
	zassert_equal(head->struct_version, EC_BATTERY_CONFIG_STRUCT_VERSION);
	expected = sizeof(*head) + head->manuf_name_size +
		   head->device_name_size + sizeof(conf);
	zassert_equal(args.response_size, expected);

	/* Verify manuf name match. */
	p += sizeof(*head);
	zassert_equal(head->manuf_name_size, strlen("LGC"));
	zassert_equal(strncmp(p, "LGC", strlen("LGC")), 0);

	/* Verify device name match. */
	p += head->manuf_name_size;
	zassert_equal(head->device_name_size, strlen("AC17A8M"));
	zassert_equal(strncmp(p, "AC17A8M", strlen("AC17A8M")), 0);

	/* Verify config match. */
	p += head->device_name_size;
	memcpy(&conf, p, sizeof(conf));
	zassert_equal(conf.fuel_gauge.fet.reg_mask, 0x2000);
	zassert_equal(conf.batt_info.voltage_max, 13134);
	zassert_equal(conf.batt_info.precharge_current, 256);
}

ZTEST_SUITE(battery_config, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
=======
/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(battery_config, test_get)
{
	uint8_t buf[BATT_CONF_MAX_SIZE];
	struct host_cmd_handler_args args;
	struct batt_conf_header *head;
	struct board_batt_params conf;
	uint8_t *p = buf;
	int expected;

	zassert_ok(ec_cmd_battery_config(&args, buf));

	/* Verify metadata. */
	head = (struct batt_conf_header *)buf;
	zassert_equal(head->struct_version, EC_BATTERY_CONFIG_STRUCT_VERSION);
	expected = sizeof(*head) + head->manuf_name_size +
		   head->device_name_size + sizeof(conf);
	zassert_equal(args.response_size, expected);

	/* Verify manuf name match. */
	p += sizeof(*head);
	zassert_equal(head->manuf_name_size, strlen("LGC"));
	zassert_equal(strncmp(p, "LGC", strlen("LGC")), 0);

	/* Verify device name match. */
	p += head->manuf_name_size;
	zassert_equal(head->device_name_size, strlen("AC17A8M"));
	zassert_equal(strncmp(p, "AC17A8M", strlen("AC17A8M")), 0);

	/* Verify config match. */
	p += head->device_name_size;
	memcpy(&conf, p, sizeof(conf));
	zassert_equal(conf.fuel_gauge.fet.reg_mask, 0x2000);
	zassert_equal(conf.batt_info.voltage_max, 13134);
	zassert_equal(conf.batt_info.precharge_current, 256);
}

ZTEST_USER(battery_config, test_buffer_too_small)
{
	uint8_t buf[sizeof(struct batt_conf_header)];
	struct host_cmd_handler_args args;

	/* Buffer smaller than required response size should be rejected */
	zassert_equal(CROS_EC_COMMAND(&args, EC_CMD_BATTERY_CONFIG, 0, NULL, 0,
				      buf, sizeof(buf)),
		      EC_RES_OVERFLOW);
}

ZTEST_SUITE(battery_config, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
>>>>>>> CHANGE (5e348512e5eb4283cd776f616460c225d2382686 battery_fuel_gauge: prevent OOB write in hc_battery_config)
