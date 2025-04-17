/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "task.h"
#include "i2c.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_DEV_NODE        DT_NODELABEL(i2c4)

#define I2C_DEV_TARGET_NODE DT_NODELABEL(i2c1_target)
#define TARGET_ADDRESS 0x52

#define I2C_DEV_TARGET2_NODE DT_NODELABEL(i2c1_target_addr2)
#define TARGET_ADDRESS2 0x62

#define I2C_DEV_TARGET3_NODE DT_NODELABEL(i2c1_target_addr3)
#define TARGET_ADDRESS3 0x72

uint32_t i2c_cfg = I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_CONTROLLER;
uint8_t w_datas[255];
uint8_t r_datas[255];
uint8_t w_datas1[256];
uint8_t r_datas1[16];

static uint8_t expected[256];
static int start_tick;
const struct device *const i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);

#if 1
static void tick_task(void)
{
	(void)memset(&r_datas, 0, sizeof(r_datas));
	if (start_tick) {
		//for (int i=0; i<10; i++) {
			if (i2c_burst_read(i2c_dev, TARGET_ADDRESS, 0xaa, r_datas, sizeof(r_datas))) {
				printf("Fail to read\n");

			}
			for(int j=0; j<sizeof(r_datas); j++) {
				//printf("axis raw data[%d]: %x\n", j, r_datas[j]);

				if (r_datas[j] != expected[j]) {
					printk("Mismatch at index %d: got 0x%02X, expected 0x%02X\n",
						j, r_datas[j], expected[j]);
				}

			}
		//}

	}
}
//DECLARE_HOOK(HOOK_TICK, tick_task, HOOK_PRIO_DEFAULT);
#endif
static int test_i2c_host(void)
{

	const struct device *i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET_NODE);
	uint32_t i2c_cfg_tmp, ret;

	ret = i2c_target_driver_register(i2c_target_dev);
	if (ret) {
		printf("I2C target register failed\n");
	}

	if (!device_is_ready(i2c_dev)) {
		printf("I2C device is not ready\n");
		return -1;
	}

	/* 1. Verify i2c_configure() */
	if (i2c_configure(i2c_dev, i2c_cfg)) {
		printf("I2C config failed\n");
		return -1;
	}

	/* 2. Verify i2c_get_config() */
	if (i2c_get_config(i2c_dev, &i2c_cfg_tmp)) {
		printf("I2C get_config failed\n");
		return -1;
	}
	if (i2c_cfg != i2c_cfg_tmp) {
		printf("I2C get_config returned invalid config\n");
		return -1;
	}

#if 1
	start_tick=1;
	for(int k=0;k<256;k++) {
		expected[k]=0xff-k;
	}

	timestamp_t t_i2chost_start, t_i2chost_end;
	uint64_t duration_us;
	t_i2chost_start = get_time();
	for(int a=0;a<1000;a++) {
		tick_task();
	}
	t_i2chost_end = get_time();
	duration_us = (t_i2chost_end.val - t_i2chost_start.val);
	printk("i2c host total duration_us %llu us\n", duration_us);
#endif
#if 0
	/* Register target address2 */
	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET2_NODE);
	ret = i2c_target_driver_register(i2c_target_dev);
	if (ret) {
		printf("I2C target register2 failed\n");
	}


	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET3_NODE);
	ret = i2c_target_driver_register(i2c_target_dev);
	if (ret) {
		printf("I2C target register3 failed\n");
	}
#endif
#if 0
	printf("[main]1.===write datas(<=255bytes FIFO mode allowed)===\n");
	printf("[main]1.w_datas=%p\n",w_datas);
	for (int i = 0; i < sizeof(w_datas); i++) {
		w_datas[i] = i;
	}

	if (i2c_write(i2c_dev, w_datas, sizeof(w_datas), TARGET_ADDRESS)) {
		printf("Fail to write 0x52\n");
		return -1;
	}

	printf("\n[main]1-1.===write datas(>255bytes PIO mode allowed)===\n");

	for (int i = 0; i < sizeof(w_datas1); i++) {
		w_datas1[i] = 255-i;
	}

	if (i2c_write(i2c_dev, w_datas1, sizeof(w_datas1), TARGET_ADDRESS)) {
		printf("Fail to write 0x62\n");
		return -1;
	}
#endif
#if 0
	printf("\n[main]2.===read datas(<=255bytes FIFO mode allowed)===\n");

	if (i2c_read(i2c_dev, r_datas, sizeof(r_datas), TARGET_ADDRESS)) {
		printf("Fail to read\n");
		return -1;
	}
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas[0], r_datas[1], r_datas[2],
				r_datas[3], r_datas[4], r_datas[5]);

	printf("\n[main]2-1.===read datas(>255bytes PIO mode allowed)===\n");

	//if (i2c_read(i2c_dev, r_datas1, sizeof(r_datas1), TARGET_ADDRESS2)) {
	//	printf("Fail to read\n");
	//	return -1;
	//}
	//printf("axis raw data: %x %x %x %x %x %x\n",
	//			r_datas1[0], r_datas1[1], r_datas1[2],
	//			r_datas1[3], r_datas1[4], r_datas1[5]);


	//printf("\n[main]3.===i2c_burst_read(read 255bytes FIFO mode allowed)===\n");
	for (int i=0; i<10; i++) {
		(void)memset(&r_datas, 0, sizeof(r_datas));

		if (i2c_burst_read(i2c_dev, TARGET_ADDRESS, 0xaa, r_datas, sizeof(r_datas))) {
			printf("Fail to read\n");
			return -1;
		}
	}
#endif
#if 0
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas[0], r_datas[1], r_datas[2],
				r_datas[3], r_datas[4], r_datas[5]);

	printf("\n[main]3-1.===i2c_burst_read(<=64bytes CQ mode allowed)===\n");

	if (i2c_burst_read(i2c_dev, TARGET_ADDRESS2, 0xaa, r_datas1, sizeof(r_datas1))) {
		printf("Fail to read\n");
		return -1;
	}
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas1[0], r_datas1[1], r_datas1[2],
				r_datas1[3], r_datas1[4], r_datas1[5]);
#endif
#if 0

	printf("\n[main]4.===i2c_burst_write(write 255bytes FIFO mode don't allow)===\n");
	for (int i = 0; i < sizeof(w_datas); i++) {
		w_datas[i] = i;
	}
	if (i2c_burst_write(i2c_dev, 0x52, 0xbb, w_datas, sizeof(w_datas))) {
		printk("Fail to write\n");
		return -1;
	}

	printf("\n[main]5.===i2c_write_read(write 255bytes FIFO mode allowed)===\n");

	for (int i = 0; i < sizeof(w_datas); i++) {
		w_datas[i] = i;
	}

	if (i2c_write_read(i2c_dev, 0x52, w_datas, sizeof(w_datas),
			   r_datas, sizeof(r_datas))) {
		printf("Fail to read\n");
		return -1;
	}
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas[0], r_datas[1], r_datas[2],
				r_datas[3], r_datas[4], r_datas[5]);
#endif
#if 0
	printf("\n[main]5.===Cros I2C: i2c_read_block(<=255bytes FIFO mode allowed)===\n");

	(void)memset(&r_datas, 0, sizeof(r_datas));

	if (i2c_read_block(1,0x52,0xcc,r_datas,sizeof(r_datas))) { //i2c_1-->port=1, i2c_4-->port=3
		printf("Fail to read\n");
		return -1;
	}
	printk("[i2c]axis raw data: %x %x %x %x %x %x\n",
				r_datas[0], r_datas[1], r_datas[2],
				r_datas[3], r_datas[4], r_datas[5]);


	printf("\n[main]6.===Cros I2C: i2c_write_block(CQ and FIFO mode don't allow)===\n");

	if (i2c_write_block(1,0x52,0xdd,w_datas,sizeof(w_datas)-1)) {
		printk("Fail to write\n");
		return -1;
	}
#endif
#if 0
	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET_NODE);
	ret = i2c_target_driver_unregister(i2c_target_dev);
	if (ret) {
		printf("I2C target unregister failed\n");
	}


	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET3_NODE);
	ret = i2c_target_driver_register(i2c_target_dev);
	if (ret) {
		printf("I2C target register3 failed\n");
	}

	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET3_NODE);
	ret = i2c_target_driver_unregister(i2c_target_dev);
	if (ret) {
		printf("I2C target unregister3 failed\n");
	}

	i2c_target_dev = DEVICE_DT_GET(I2C_DEV_TARGET2_NODE);
	ret = i2c_target_driver_unregister(i2c_target_dev);
	if (ret) {
		printf("I2C target unregister2 failed\n");
	}
#endif
#if 0
	printf("\n[main]4.===i2c_burst_read(read 255bytes FIFO mode allowed)===\n");

	(void)memset(&r_datas, 0, sizeof(r_datas));

	if (i2c_burst_read(i2c_dev, TARGET_ADDRESS, 0xaa, r_datas, sizeof(r_datas))) {
		printf("Fail to read\n");
		//return -1;
	}
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas[0], r_datas[1], r_datas[2],
				r_datas[3], r_datas[4], r_datas[5]);

	printf("\n[main]3-1.===i2c_burst_read(<=64bytes CQ mode allowed)===\n");

	if (i2c_burst_read(i2c_dev, TARGET_ADDRESS2, 0xaa, r_datas1, sizeof(r_datas1))) {
		printf("Fail to read\n");
		return -1;
	}
	printf("axis raw data: %x %x %x %x %x %x\n",
				r_datas1[0], r_datas1[1], r_datas1[2],
				r_datas1[3], r_datas1[4], r_datas1[5]);
#endif

	return 0;
}

static void board_init(void)
{
	int ret;

	printf("[main]===test_i2c_host to target===\n");
	ret = test_i2c_host();
	if(ret)
		printf("[main]fail to access\n");
	else
		printf("[main]i2c transfer sucess!\n\n\n");

}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_POST_I2C);
