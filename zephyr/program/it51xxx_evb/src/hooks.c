/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/kernel.h>
#include <zephyr/pm/policy.h>
#include "common.h"
#include "hooks.h"

#include <soc_common.h>

static struct k_thread tdata2;
static struct k_thread tdata1;
 struct k_sem sem_thread1;
 struct k_sem sem_thread2;

static K_THREAD_STACK_DEFINE(test_thread_stack, 512);
static K_THREAD_STACK_DEFINE(test_thread_stack2, 512);

static void thread_entry2(void *p1, void *p2, void *p3)
{
	//printk("thread2: k_current_get=%p\n",k_current_get());
	while (1) {
		//printk("thread_entry2\n");
		k_sem_take(&sem_thread2, K_FOREVER);

		//ECREG(EC_REG_BASE_ADDR + 0x1603) |= BIT(4);
		//ECREG(EC_REG_BASE_ADDR + 0x1603) &= ~BIT(4);
		//printk("thread_entry2 end\n");
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
		//ECREG(EC_REG_BASE_ADDR + 0x1603) |= BIT(6);
		//ECREG(EC_REG_BASE_ADDR + 0x1603) &= ~BIT(6);
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

	k_sem_init(&sem_thread1, 0, 1);
	k_sem_init(&sem_thread2, 0, 1);

	//ECREG(0xf0161a) = 0x40;
	//ECREG(0xf01602) |= BIT(2);
	//ECREG(0xf01602) &= ~BIT(2);
	k_busy_wait(50000);
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
}
DECLARE_HOOK(HOOK_INIT, init_task, HOOK_PRIO_DEFAULT);

#if 0
static void tick_task(void)
{
	k_busy_wait(500000);
}
DECLARE_HOOK(HOOK_TICK, tick_task, HOOK_PRIO_DEFAULT);
#endif
