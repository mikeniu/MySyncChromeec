/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "host_command.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>  

LOG_MODULE_REGISTER(pwrmon, LOG_LEVEL_INF);

#define PWRMON_DEVICES_COUNT DT_NUM_INST_STATUS_OKAY()

struct pwrmon_data {
    const struct sensor *power_monitors;
    int pwrmon_count;
    int sample_rate;
};

//static struct pwrmon_data pwrmon_data;

// SYS_INIT(pwrmon_dev, "pwrmon_drv", pwrmon_driver_init, NULL,
// 	      &pwrmon_data, NULL, POST_KERNEL,
// 	      CONFIG_APPLICATION_INIT_PRIORITY, NULL);


static int pwrmon_init(void)
{
    LOG_INF("DUPA");
//    pwrmon_data->pwrmon_count = 6;
//    for(int i = 0; i < data->pwrmon_count; ++i) {
//        data->power_monitors[i] = DEVICE_DT_GET();
//    }

	return 0;
}
SYS_INIT(pwrmon_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

// static enum ec_status pwrmon_start()
// {
//     for (int i = 0; pwrmon_data.pwrmon_count, ++i) {
//         pwrmon_data.power_monitors[i]->
//     }
// 
//     return EC_RES_SUCCESS;
// }

static enum ec_status hc_pwrmon_handler(struct host_cmd_handler_args *args)
{
	const struct ec_params_pwrmon *p = args->params;
	struct ec_response_pwrmon *r = args->response;
    enum ec_status ret = EC_RES_SUCCESS;

	switch (p->cmd) {
	case EC_PWRMON_GET_CHANNEL_COUNT:
        LOG_INF("EC_PWRMON_GET_CHANNEL_COUNT");
        LOG_INF("%d", sizeof(struct ec_response_pwrmon));
        r->channel_count.count = 1;
        args->response_size = sizeof(struct pwrmon_get_channel_count);
		break;

	case EC_PWRMON_DUMP_INFO:
        LOG_INF("EC_PWRMON_DUMP_INFO");
        r->dump_info.channel_id = 1;
        strcpy(r->dump_info.channel_name, "ppvar_3v3");
        args->response_size = sizeof(struct pwrmon_dump_info);
		break;

	case EC_PWRMON_GET_RATE:
        LOG_INF("EC_PWRMON_GET_RATE");
        r->get_rate.rate = 1024;
        args->response_size = sizeof(struct pwrmon_get_rate);
		break;

	case EC_PWRMON_SET_RATE:
        LOG_INF("EC_PWRMON_SET_RATE");
		break;

	case EC_PWRMON_START:
        LOG_INF("EC_PWRMON_START");
		break;

	case EC_PWRMON_STOP:
        LOG_INF("EC_PWRMON_STOP");
		break;

	case EC_PWRMON_GET:
        LOG_INF("EC_PWRMON_GET");
        r->channel_info.channel_id = 1;
        r->channel_info.samples = 2137;
        r->channel_info.value = 7312;
        args->response_size = sizeof(struct pwrmon_channel_info);
		break;

	default:
		break;
	}
    
    LOG_INF("%d %d", sizeof(struct ec_response_pwrmon), args->response_size);
	return ret;
}
DECLARE_HOST_COMMAND(EC_CMD_PWRMON, hc_pwrmon_handler, EC_VER_MASK(0));
