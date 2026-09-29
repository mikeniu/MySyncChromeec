/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <soc.h>

#include <zephyr/devicetree.h>

#if 0
static const struct pwm_dt_spec irq0_test_pwm =
	PWM_DT_SPEC_GET(DT_NODELABEL(pwm_led_test));
static const struct gpio_dt_spec irq0_test_input =
	GPIO_DT_SPEC_GET(DT_NODELABEL(irq0_test_input), gpios);

static struct gpio_callback irq0_test_callback;
static volatile uint32_t irq0_test_irq_count;
#endif

#define GPIO_B2_PIN 2
#define I2C_READ_SIZE 6
#define I2C_TARGET_ADDRESS 0x52
#define I2C_WRITE_SIZE 3
#define IRQ_LOCK_THREAD_STACK_SIZE 1024

static const struct gpio_dt_spec gpio_b2 = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = GPIO_B2_PIN,
};

/* i2c0 device thread */
static const struct device *const i2c_host = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static void irq_lock_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t read_data[I2C_READ_SIZE];

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	//if (!gpio_is_ready_dt(&gpio_b2)) {
	//	return;
	//}

	if (!device_is_ready(i2c_host)) {
		printk("I2C host is not ready\n");
		return;
	}

	//printk("start irq_lock_thread:\n");

	while (true) {
		//unsigned int key = irq_lock();

		//irq_unlock(key);

		int rv = i2c_read(i2c_host, read_data, sizeof(read_data),
				  I2C_TARGET_ADDRESS);

		rv = i2c_write(i2c_host, read_data, I2C_WRITE_SIZE,
			       I2C_TARGET_ADDRESS);
		if (rv != 0) {
			printk("%s 0x%02x write failed: %d\n",
			       i2c_host->name, I2C_TARGET_ADDRESS, rv);
		} else {
			//printk("I2C 0x%02x write failed: %d\n",
			//       I2C_TARGET_ADDRESS, rv);
		}

		k_sleep(K_USEC(1000));
	}

}

K_THREAD_DEFINE(irq_lock_thread_id, IRQ_LOCK_THREAD_STACK_SIZE,
		irq_lock_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/* i2c1 device thread */
static const struct device *const i2c1_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static void i2c_dev1_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t read_data[I2C_READ_SIZE];

	if (!device_is_ready(i2c1_dev)) {
		printk("I2C host is not ready\n");
		return;
	}

	while (true) {

		int rv = i2c_read(i2c1_dev, read_data, sizeof(read_data),
				  I2C_TARGET_ADDRESS);

		rv = i2c_write(i2c1_dev, read_data, I2C_WRITE_SIZE,
			       I2C_TARGET_ADDRESS);
		if (rv != 0) {
			printk("%s 0x%02x write failed: %d\n",
			       i2c1_dev->name, I2C_TARGET_ADDRESS, rv);
		} else {
			//printk("I2C 0x%02x write failed: %d\n",
			//       I2C_TARGET_ADDRESS, rv);
		}

		k_sleep(K_USEC(5000));
	}

}

K_THREAD_DEFINE(i2c_dev1_thread_id, IRQ_LOCK_THREAD_STACK_SIZE,
		i2c_dev1_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/* i2c2 device thread */
static const struct device *const i2c2_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
static void i2c_dev2_thread(void *arg1, void *arg2, void *arg3)
{
	uint8_t read_data[I2C_READ_SIZE];

	if (!device_is_ready(i2c2_dev)) {
		printk("I2C host is not ready\n");
		return;
	}

	while (true) {

		int rv = i2c_read(i2c2_dev, read_data, sizeof(read_data),
				  I2C_TARGET_ADDRESS);

		rv = i2c_write(i2c2_dev, read_data, I2C_WRITE_SIZE,
			       I2C_TARGET_ADDRESS);
		if (rv != 0) {
			printk("%s 0x%02x write failed: %d\n",
			       i2c2_dev->name, I2C_TARGET_ADDRESS, rv);
		} else {
			//printk("I2C 0x%02x write failed: %d\n",
			//       I2C_TARGET_ADDRESS, rv);
		}

		k_sleep(K_USEC(10000));
	}

}

K_THREAD_DEFINE(i2c_dev2_thread_id, IRQ_LOCK_THREAD_STACK_SIZE,
		i2c_dev2_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

#if 0
static void irq0_test_gpio_isr(const struct device *port,
			       struct gpio_callback *callback, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	/* Keep this ISR minimal so MEIP deassertion can race with mret. */
	irq0_test_irq_count++;
}

static int irq0_test_init(void)
{
	int rv;

	if (!pwm_is_ready_dt(&irq0_test_pwm)) {
		printk("irq0test: PWM0 is not ready\n");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&irq0_test_input)) {
		printk("irq0test: GPC6 is not ready\n");
		return -ENODEV;
	}

	rv = gpio_pin_configure_dt(&irq0_test_input, GPIO_INPUT);
	if (rv != 0) {
		printk("irq0test: failed to configure GPC6: %d\n", rv);
		return rv;
	}

	gpio_init_callback(&irq0_test_callback, irq0_test_gpio_isr,
			   BIT(irq0_test_input.pin));
	rv = gpio_add_callback(irq0_test_input.port, &irq0_test_callback);
	if (rv != 0) {
		printk("irq0test: failed to add GPC6 callback: %d\n", rv);
		return rv;
	}

	rv = gpio_pin_interrupt_configure_dt(&irq0_test_input, GPIO_INT_EDGE_RISING);
	if (rv != 0) {
		printk("irq0test: failed to enable GPC6 interrupt: %d\n", rv);
		return rv;
	}

	rv = pwm_set_dt(&irq0_test_pwm, PWM_USEC(20), PWM_USEC(20) / 2U);
	if (rv != 0) {
		chip_permit_idle();
		(void)gpio_pin_interrupt_configure_dt(&irq0_test_input,
						 GPIO_INT_DISABLE);
		printk("irq0test: failed to start PWM0: %d\n", rv);
		return rv;
	}

	printk("irq0test: connect GPA0(pwm) to GPD0(irq)\n");
	return 0;
}
SYS_INIT(irq0_test_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int command_irq0_test(int argc, const char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ccprintf("irq0test GPIO interrupts: %u\n",
		 (unsigned int)irq0_test_irq_count);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(irq0test, command_irq0_test, "",
			"Show the IRQ0 reproduction GPIO interrupt count");
#endif


static volatile uint32_t irq155_test_expiry_count;

static void irq155_test_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	(void)gpio_pin_toggle_dt(&gpio_b2);

	irq155_test_expiry_count++;
}
K_TIMER_DEFINE(irq155_test_timer, irq155_test_expiry, NULL);

static int irq155_test_init(void)
{
	k_timer_start(&irq155_test_timer, K_MSEC(1), K_MSEC(1));
	printk("irq155test: TIMER3 10 ms\n");
	return 0;
}
SYS_INIT(irq155_test_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int command_irq155_test(int argc, const char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ccprintf("irq155test timer expiries: %u\n",
		 (unsigned int)irq155_test_expiry_count);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(irq155test, command_irq155_test, "",
			"Show the IRQ155 timer expiry count");
