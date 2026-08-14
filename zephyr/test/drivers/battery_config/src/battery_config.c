/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test battery info in CBI
 */

#include "battery_fuel_gauge.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

const struct board_batt_params *get_batt_params(void);

static struct board_batt_params conf_in_cbi = {
	.fuel_gauge = {
		.ship_mode = {
			.reg_addr = 0xaa,
			.reg_data = {
				[0] = 0x89ab,
				[1] = 0xcdef,
			},
		},
	},
	.batt_info = {
		.voltage_max = 8400,
		.voltage_normal = 7400,
		.voltage_min = 6000,
		.precharge_current = 64, /* mA */
		.start_charging_min_c = 0,
		.start_charging_max_c = 50,
		.charging_min_c = 0,
		.charging_max_c = 50,
		.discharging_min_c = -20,
		.discharging_max_c = 60,
	},
};

static const char *manuf_in_batt = "LGC";
static const char *device_in_batt = "AC17A8M";

int battery_manufacturer_name(char *dest, int size)
{
	if (!manuf_in_batt)
		return EC_ERROR_UNKNOWN;
	strncpy(dest, manuf_in_batt, size);
	return EC_SUCCESS;
}

int battery_device_name(char *dest, int size)
{
	if (!device_in_batt)
		return EC_ERROR_UNKNOWN;
	strncpy(dest, device_in_batt, size);
	return EC_SUCCESS;
}

static void battery_config_before(void *fixture)
{
	cbi_create();
	cbi_write();

	manuf_in_batt = "LGC";
	device_in_batt = "AC17A8M";
}

ZTEST_SUITE(battery_config_cbi, drivers_predicate_post_main, NULL,
	    battery_config_before, NULL, NULL);

static void cbi_set_batt_conf(const struct board_batt_params *conf,
			      const char *manuf_name, const char *device_name)
{
	uint8_t buf[BATT_CONF_MAX_SIZE];
	struct batt_conf_header *head = (void *)buf;
	void *p = buf;
	uint8_t size;

	head->struct_version = 0;
	head->manuf_name_size = strlen(manuf_name);
	head->device_name_size = strlen(device_name);

	p += sizeof(*head);
	memcpy(p, manuf_name, head->manuf_name_size);
	p += head->manuf_name_size;
	memcpy(p, device_name, head->device_name_size);
	p += head->device_name_size;
	memcpy(p, conf, sizeof(*conf));

	size = sizeof(*head) + head->manuf_name_size + head->device_name_size +
	       sizeof(*conf);
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, buf, size);
}

ZTEST(battery_config_cbi, test_power_on_reset)
{
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
}

ZTEST(battery_config_cbi, test_manuf_name_mismatch)
{
	manuf_in_batt = "xyz";
	cbi_set_batt_conf(&conf_in_cbi, "foo", "");
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
}

ZTEST(battery_config_cbi, test_empty_device_name)
{
	const struct board_batt_params *conf;

	manuf_in_batt = "xyz";
	cbi_set_batt_conf(&conf_in_cbi, manuf_in_batt, "");
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, manuf_in_batt), 0);
}

ZTEST(battery_config_cbi, test_device_name_mismatch)
{
	manuf_in_batt = "xyz";
	cbi_set_batt_conf(&conf_in_cbi, manuf_in_batt, "foo");
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
}

ZTEST(battery_config_cbi, test_match_in_cbi)
{
	const struct board_batt_params *conf;

	manuf_in_batt = "xyz";
	cbi_set_batt_conf(&conf_in_cbi, manuf_in_batt, device_in_batt);
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, manuf_in_batt), 0);
	zassert_equal(strcmp(get_batt_conf()->device_name, device_in_batt), 0);
}

ZTEST(battery_config_cbi, test_search_fw_first)
{
	manuf_in_batt = board_battery_info[0].manuf_name;
	device_in_batt = board_battery_info[0].device_name;
	cbi_set_batt_conf(&conf_in_cbi, manuf_in_batt, device_in_batt);
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
}

ZTEST(battery_config_cbi, test_device_name_as_prefix)
{
	const struct board_batt_params *conf;

	manuf_in_batt = "xyz";
	cbi_set_batt_conf(&conf_in_cbi, manuf_in_batt, "C214-43");
	device_in_batt = "C214-43 123";
	init_battery_type();
	conf = get_batt_params();
	zassert_equal(memcmp(conf, &conf_in_cbi, sizeof(*conf)), 0);
	zassert_equal(strcmp(get_batt_conf()->manuf_name, manuf_in_batt), 0);
	zassert_equal(strcmp(get_batt_conf()->device_name, "C214-43"), 0);
}

ZTEST(battery_config_cbi, test_manuf_name_not_found)
{
	manuf_in_batt = NULL;
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	manuf_in_batt = "AS1GUXd3KB";
}

ZTEST(battery_config_cbi, test_device_name_not_found)
{
	device_in_batt = NULL;
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	device_in_batt = "C214-43";
}

ZTEST(battery_config_cbi, test_batt_conf_main_invalid)
{
	struct batt_conf_header head;

	head.struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION + 1;
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
	head.struct_version = EC_BATTERY_CONFIG_STRUCT_VERSION;

	head.manuf_name_size = 0xff;
	cbi_set_board_info(CBI_TAG_BATTERY_CONFIG, (void *)&head, sizeof(head));
	init_battery_type();
	zassert_equal_ptr(get_batt_params(), &board_battery_info[0].config);
}
