/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/drivers/espi.h>
#include <x86_non_dsx_common_pwrseq_sm_handler.h>

#include "signal_vw.h"

#define MY_COMPAT	intel_ap_pwrseq_vw

#if HAS_VW_SIGNALS

LOG_MODULE_DECLARE(ap_pwrseq, CONFIG_AP_PWRSEQ_LOG_LEVEL);

#define INIT_ESPI_SIGNAL(id)					\
{								\
	.espi_signal = DT_STRING_UPPER_TOKEN(id, virtual_wire),	\
	.signal = PWR_SIGNAL_ENUM(id),				\
	.invert = DT_PROP(id, vw_invert),			\
},

/*
 * Struct containing the eSPI virtual wire config.
 */
struct vw_config {
	uint8_t espi_signal;	/* associated VW signal */
	uint8_t signal;		/* power signal */
	bool invert;		/* Invert the signal value */
};

const static struct vw_config vw_config[] = {
DT_FOREACH_STATUS_OKAY(MY_COMPAT, INIT_ESPI_SIGNAL)
};

/*
 * Current signal value.
 */
static atomic_t signal_data;
/*
 * Mask of valid signals. A signal is considered valid once an
 * initial value has been received for it.
 *
 * Values are only invalid following an EC reset; all other reset events
 * (including eSPI reset and PLTRST) leave virtual wires in well-defined states
 * but the platform may be in a non-default state on EC boot.
 */
static atomic_t signal_valid;

#define espi_dev DEVICE_DT_GET(DT_CHOSEN(intel_ap_pwrseq_espi))

BUILD_ASSERT(ARRAY_SIZE(vw_config) <= (sizeof(atomic_t) * 8));

/**
 * Set the physical level of a virtual wire power signal.
 *
 * For inverted signals, the logical value (which gets stored) will be the
 * inverse of the provided value.
 */
static void vw_set_level(enum espi_vwire_signal espi_signal, bool value)
{
	for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
		if (espi_signal != vw_config[i].espi_signal) {
			continue;
		}
		value = vw_config[i].invert ? !value : !!value;

		atomic_set_bit_to(&signal_data, i, value);
		atomic_set_bit(&signal_valid, i);
		power_signal_interrupt(vw_config[i].signal, value);
		break;
	}
}

/**
 * Virtual wires that get cleared on eSPI reset.
 *
 * These are defined by section 5.2.2.2 of the eSPI 1.0 specification, providing
 * the reset values for virtual wires that are reset by assertion of the eSPI
 * Reset# signal (but not PLTRST#). Only VWs sent by the host are included
 * because this module only supports receiving VW values, not setting them.
 *
 * The polarity of the signals varies, but the reset state of each one is low
 * (which is asserted for the active-low signals).
 */
const static enum espi_vwire_signal espi_reset_signals[] = {
	ESPI_VWIRE_SIGNAL_SLP_S3,
	ESPI_VWIRE_SIGNAL_SLP_S4,
	ESPI_VWIRE_SIGNAL_SLP_S5,
	ESPI_VWIRE_SIGNAL_OOB_RST_WARN,
	ESPI_VWIRE_SIGNAL_PLTRST,
	ESPI_VWIRE_SIGNAL_SUS_STAT,
};

static void espi_handler(const struct device *dev,
			 struct espi_callback *cb,
			 struct espi_event event)
{
	LOG_DBG("ESPI event type 0x%x %d:%d", event.evt_type,
		event.evt_details, event.evt_data);
	switch (event.evt_type) {
	default:
		__ASSERT(0, "ESPI unknown event type: %d",
			 event.evt_type);
		break;

	case ESPI_BUS_RESET:
		/*
		 * Bus reset (not PLTRST): reset virtual wires to their default
		 * states when asserted.
		 */
		if (event.evt_data == 0) {
			LOG_INF("eSPI bus reset asserted");
			for (int i = 0; i < ARRAY_SIZE(espi_reset_signals);
			     i++) {
				vw_set_level(espi_reset_signals[i], 0);
			}
		}
		break;

	case ESPI_BUS_EVENT_VWIRE_RECEIVED:
		vw_set_level(event.evt_details, event.evt_data);
		break;
	}
}

int power_signal_vw_get(enum pwr_sig_vw vw)
{
	if (vw < 0 || vw >= ARRAY_SIZE(vw_config) ||
	    !atomic_test_bit(&signal_valid, vw)) {
		return -EINVAL;
	}
	return atomic_test_bit(&signal_data, vw);
}

void power_signal_vw_init(void)
{
	static struct espi_callback espi_cb;

	/* Assumes ESPI device is already configured. */

	/* Configure handler for eSPI events */
	espi_init_callback(&espi_cb, espi_handler,
			   ESPI_BUS_EVENT_CHANNEL_READY |
			   ESPI_BUS_EVENT_VWIRE_RECEIVED);
	espi_add_callback(espi_dev, &espi_cb);
	/*
	 * Check whether the bus is ready, and if so,
	 * initialise the current values of the signals.
	 */
	if (espi_get_channel_status(espi_dev, ESPI_CHANNEL_VWIRE)) {
		for (int i = 0; i < ARRAY_SIZE(vw_config); i++) {
			uint8_t vw_value;

			if (espi_receive_vwire(espi_dev,
					   vw_config[i].espi_signal,
					   &vw_value) == 0) {
				atomic_set_bit_to(&signal_data, i,
					vw_config[i].invert
						? !vw_value
						: !!vw_value);
				atomic_set_bit(&signal_valid, i);

			}
		}
	}
}

#endif /* HAS_VW_SIGNALS */
