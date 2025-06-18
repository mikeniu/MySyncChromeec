/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include "common.h"
#include "hooks.h"
#include "gpio.h"

#include <soc_common.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/it51xxx_flash_api_ex.h>
const struct device *const flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
//const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(manual_flash_1k));

static struct k_thread tdata2;
static struct k_thread tdata1;
 struct k_sem sem_thread1;
 struct k_sem sem_thread2;

static K_THREAD_STACK_DEFINE(test_thread_stack, 512);
static K_THREAD_STACK_DEFINE(test_thread_stack2, 512);

static uint8_t __aligned(0x1000) w_data[256];
//static uint8_t r_buf[256]={0};
static void thread_entry2(void *p1, void *p2, void *p3)
{
	//printk("thread2: k_current_get=%p\n",k_current_get());
	while (1) {
		printk("thread_entry2\n");
		k_sem_take(&sem_thread2, K_FOREVER);

		if(ECREG(0xf01605) & BIT(1)) {
			printk("0\n");
			flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS0, (uintptr_t)NULL, NULL);
		} else {
			printk("1\n");
			flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS1, (uintptr_t)NULL, NULL);
		}
	}
}

void test_task2()
{
	//printk("[thread]k_thread_create2\n");
	k_thread_create(&tdata2, test_thread_stack2, 512, thread_entry2,
			NULL, NULL, NULL, 10, 0, K_NO_WAIT);
}

static void thread_entry1(void *p1, void *p2, void *p3)
{
	//printk("thread1: k_current_get=%p\n",k_current_get());
	while (1) {
		//printk("thread_entry1\n");
		//flash_read(flash_dev, 0x6000, r_buf, 0x100);
#if 0
		flash_ex_op(flash_dev, FLASH_IT51XXX_INTERNAL, (uintptr_t)NULL, NULL);
		//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
		flash_erase(flash_dev, 0x40000, 0x1000);
		flash_write(flash_dev, 0x40000, w_data, sizeof(w_data));

		flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS0, (uintptr_t)NULL, NULL);
		//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
		flash_erase(flash_dev, 0x5000, 0x1000);
		flash_write(flash_dev, 0x5000, w_data, sizeof(w_data));

		flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS1, (uintptr_t)NULL, NULL);
		//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
		flash_erase(flash_dev, 0x12000, 0x1000);
		flash_write(flash_dev, 0x12000, w_data, sizeof(w_data));
#endif
		k_sem_give(&sem_thread2);
		k_sem_take(&sem_thread1, K_FOREVER);

		printk("thread_entry1 end\n");

		//last_prio = k_thread_priority_get(k_current_get());
		//k_msleep(500);
	}
}

void test_task1()
{
	//printk("[thread]k_thread_create1\n");
	k_thread_create(&tdata1, test_thread_stack, 512, thread_entry1,
			NULL, NULL, NULL, 19, 0, K_NO_WAIT);

	//k_thread_start(&tdata);
	//k_wakeup(test_thread_id);
}

static void init_task(void)
{
	//chip_block_idle();
	//pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);

	for(int k=0;k<256;k++) {
		w_data[k]=0xff-k;
	}

	k_sem_init(&sem_thread1, 0, 1);
	k_sem_init(&sem_thread2, 0, 1);

	//ECREG(0xf0161a) = 0x40;
	//ECREG(0xf01602) |= BIT(2);
	//ECREG(0xf01602) &= ~BIT(2);
	//k_busy_wait(50000);
	//ECREG(0xf01602) |= BIT(2);
	//ECREG(0xf01602) &= ~BIT(2);
	//printk("delay 2s main\n");

	test_task1();
	test_task2();
#if 0
	k_busy_wait(100);
	printk("delay 100u main\n");

	k_busy_wait(200);
	printk("delay 200u main\n");

	k_busy_wait(300);
	printk("delay 300u main\n");

	k_busy_wait(1000000);
	printk("delay 1000u main\n");

	k_busy_wait(200);
	printk("delay 200u main\n");
#endif

	gpio_enable_interrupt(GPIO_TEST_M1K);
}
DECLARE_HOOK(HOOK_INIT, init_task, HOOK_PRIO_DEFAULT);

void m1k_interrupt(enum gpio_signal signal)
{
	printk("interrupt m1k test\n");

	flash_ex_op(flash_dev, FLASH_IT51XXX_INTERNAL, (uintptr_t)NULL, NULL);
	//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
	flash_erase(flash_dev, 0x30000, 0x1000);
	flash_write(flash_dev, 0x30000, w_data, sizeof(w_data));

	flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS0, (uintptr_t)NULL, NULL);
	//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
	flash_erase(flash_dev, 0x7000, 0x1000);
	flash_write(flash_dev, 0x7000, w_data, sizeof(w_data));

	flash_ex_op(flash_dev, FLASH_IT51XXX_EXTERNAL_FSPI_CS1, (uintptr_t)NULL, NULL);
	//flash_ex_op(flash_dev, FLASH_IT51XXX_ADDR_3B, (uintptr_t)NULL, NULL);
	flash_erase(flash_dev, 0x8000, 0x1000);
	flash_write(flash_dev, 0x8000, w_data, sizeof(w_data));
}

#if 0
static void tick_task(void)
{
	k_busy_wait(500000);
}
DECLARE_HOOK(HOOK_TICK, tick_task, HOOK_PRIO_DEFAULT);
#endif
