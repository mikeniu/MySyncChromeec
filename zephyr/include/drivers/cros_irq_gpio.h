#ifndef ZEPHYR_INCLUDE_DRIVERS_CROS_IRQ_GPIO_H_
#define ZEPHYR_INCLUDE_DRIVERS_CROS_IRQ_GPIO_H_

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/slist.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irq_gpio_callback_handler)(const struct device *dev, void *data);

struct irq_gpio_callback {
	const struct device *dev;
	irq_gpio_callback_handler handler;
	uint8_t pin_state;
	void *user_data;
};

#define CROS_IRQ_GPIO_CALLBACK_DEFINE_NAMED(_dev, _handler, _user_data, name) \
	static const STRUCT_SECTION_ITERABLE(irq_gpio_callback,              \
					     _irq_gpio_callback_##name) = { \
		.dev = _dev,                                                \
		.handler = _handler,         \
		.user_data = _user_data,                                             \
	}

#define CROS_IRQ_GPIO_CALLBACK_DEFINE(_dev, _callback, _user_data)         \
	CROS_IRQ_GPIO_CALLBACK_DEFINE_NAMED(_dev, _callback, _user_data, _callback)

typedef int (*irq_gpio_enable_fn)(const struct device *dev);
typedef int (*irq_gpio_disable_fn)(const struct device *dev);
typedef int (*irq_gpio_pin_get_fn)(const struct device *dev);
typedef int (*irq_gpio_configure_fn)(const struct device *dev, gpio_flags_t flags);
__subsystem struct irq_gpio_driver_api {
	irq_gpio_enable_fn enable;
	irq_gpio_disable_fn disable;
	irq_gpio_configure_fn configure;
	irq_gpio_pin_get_fn pin_get;
};

static inline int irq_gpio_enable(const struct device *dev)
{
	const struct irq_gpio_driver_api *api =
		(const struct irq_gpio_driver_api *)dev->api;

	return api->enable(dev);
}

static inline int irq_gpio_disable(const struct device *dev)
{
	const struct irq_gpio_driver_api *api =
		(const struct irq_gpio_driver_api *)dev->api;

	return api->disable(dev);
}

static inline int irq_gpio_configure(const struct device *dev,
				     gpio_flags_t flags)
{
	const struct irq_gpio_driver_api *api =
		(const struct irq_gpio_driver_api *)dev->api;

	return api->configure(dev, flags);
}

static inline int irq_gpio_pin_get(const struct device *dev)
{
	const struct irq_gpio_driver_api *api =
		(const struct irq_gpio_driver_api *)dev->api;

	return api->pin_get(dev);
}
#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CROS_IRQ_GPIO_H_ */
