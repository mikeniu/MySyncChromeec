/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_irq_gpio

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/cros_irq_gpio.h>

LOG_MODULE_REGISTER(cros_irq_gpio, LOG_LEVEL_ERR);

struct cros_irq_gpio_data {
	struct gpio_callback gpio_cb;
	const struct irq_gpio_callback *irq_cb;
	struct k_work_delayable dworker;
	uint8_t pin_state;
};

struct cros_irq_gpio_config {
	const struct gpio_dt_spec gpio;
	gpio_flags_t flags;
	uint32_t debounce_ms;
	bool enable_on_init;
	bool fire_on_init;
};

static void cros_irq_gpio_fire_callback(const struct irq_gpio_callback *irq_cb)
{
	if (irq_cb && irq_cb->handler) {
		irq_cb->handler(irq_cb->dev, irq_cb->user_data);
	}
}

static void cros_irq_gpio_drv_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct cros_irq_gpio_data *data = CONTAINER_OF(dwork, struct cros_irq_gpio_data, dworker);
	const struct irq_gpio_callback *irq_cb = data->irq_cb;
	const struct cros_irq_gpio_config *cfg = irq_cb->dev->config;

	if (data->pin_state == gpio_pin_get_dt(&cfg->gpio)) {
		cros_irq_gpio_fire_callback(irq_cb);
	}
}

static void cros_irq_gpio_drv_cb(const struct device *dev, struct gpio_callback *cb,
				 uint32_t pins)
{
	struct cros_irq_gpio_data *data = CONTAINER_OF(cb, struct cros_irq_gpio_data, gpio_cb);
	const struct irq_gpio_callback *irq_cb = data->irq_cb;
	const struct cros_irq_gpio_config *cfg = irq_cb->dev->config;

	if (cfg->debounce_ms > 0) {
		data->pin_state = gpio_pin_get_dt(&cfg->gpio);
		k_work_reschedule(&data->dworker, K_MSEC(cfg->debounce_ms));
		return;
	}

	cros_irq_gpio_fire_callback(irq_cb);
}

static int cros_irq_gpio_drv_enable(const struct device *dev)
{
	const struct cros_irq_gpio_config *cfg = dev->config;

	return gpio_pin_interrupt_configure_dt(&cfg->gpio,
					     cfg->flags);
}

static int cros_irq_gpio_drv_disable(const struct device *dev)
{
	const struct cros_irq_gpio_config *cfg = dev->config;

	return gpio_pin_interrupt_configure_dt(&cfg->gpio,
					     GPIO_INT_DISABLE);
}

static int cros_irq_gpio_drv_configure(const struct device *dev,
					gpio_flags_t flags)
{
	/* TODO: apply flags to the underlying GPIO */
	return -ENOSYS;
}

static int cros_irq_gpio_drv_pin_get(const struct device *dev)
{
	const struct cros_irq_gpio_config *cfg = dev->config;

	return gpio_pin_get_dt(&cfg->gpio);
}

static const struct irq_gpio_driver_api cros_irq_gpio_api = {
	.enable = cros_irq_gpio_drv_enable,
	.disable = cros_irq_gpio_drv_disable,
	.configure = cros_irq_gpio_drv_configure,
	.pin_get = cros_irq_gpio_drv_pin_get,
};

static int cros_irq_gpio_init(const struct device *dev)
{
	const struct cros_irq_gpio_config *cfg = dev->config;
	struct cros_irq_gpio_data *data = dev->data;
	int rv;

	if (!gpio_is_ready_dt(&cfg->gpio)) {
		LOG_ERR("%s: GPIO device not ready", dev->name);
		return -ENODEV;
	}

	rv = gpio_pin_configure_dt(&cfg->gpio, GPIO_INPUT);
	if (rv < 0) {
		LOG_ERR("%s: Unable to configure GPIO", dev->name);
		return rv;
	}

	gpio_init_callback(&data->gpio_cb, cros_irq_gpio_drv_cb,
			   BIT(cfg->gpio.pin));

	rv = gpio_add_callback(cfg->gpio.port, &data->gpio_cb);
	if (rv < 0) {
		LOG_ERR("%s: Unable to add callback", dev->name);
		return rv;
	}
	
	if (cfg->debounce_ms) {
		k_work_init_delayable(&data->dworker, cros_irq_gpio_drv_work);
	}

	STRUCT_SECTION_FOREACH(irq_gpio_callback, cb)
	{
		if (cb->dev == dev) {
			data->irq_cb = cb;
		}
	}

	if (cfg->fire_on_init) {
		cros_irq_gpio_fire_callback(data->irq_cb);
	}

	if (cfg->enable_on_init) {
		cros_irq_gpio_drv_enable(dev);
	}

	return 0;
}

/* Create one device instance per child node of cros-ec,irq-gpio */
#define CROS_IRQ_GPIO_CHILD_DEFINE(node_id)  \
	static struct cros_irq_gpio_data  \
		cros_irq_gpio_data_##node_id;  \
	static const struct cros_irq_gpio_config  \
		cros_irq_gpio_cfg_##node_id = {  \
			.gpio        = GPIO_DT_SPEC_GET(node_id, gpios),  \
			.flags       = DT_PROP_OR(node_id, flags, 0),  \
			.debounce_ms = DT_PROP(node_id, debounce_ms),  \
			.enable_on_init = DT_PROP(node_id, enable_on_init),  \
			.fire_on_init = DT_PROP(node_id, fire_on_init),  \
		};  \
	DEVICE_DT_DEFINE(node_id, cros_irq_gpio_init, NULL,  \
			&cros_irq_gpio_data_##node_id,  \
			&cros_irq_gpio_cfg_##node_id,  \
			POST_KERNEL, CONFIG_IRQ_GPIO_INIT_PRIORITY,  \
			&cros_irq_gpio_api);

#define CROS_IRQ_GPIO_NODE_DEF(inst) \
	DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(inst), \
		CROS_IRQ_GPIO_CHILD_DEFINE)

DT_INST_FOREACH_STATUS_OKAY(CROS_IRQ_GPIO_NODE_DEF)
