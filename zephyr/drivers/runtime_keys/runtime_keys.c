/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_runtime_keys

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <chipset.h>
#include <drivers/runtime_keys.h>
#include <drivers/vivaldi_kbd.h>
#include <dt-bindings/kbd.h>
#include <keyboard_protocol.h>
#include <system.h>

LOG_MODULE_REGISTER(runtime_keys, LOG_LEVEL_INF);

#define CROS_EC_KEYBOARD_NODE DT_INST_PARENT(0)

static uint32_t runtime_keys_counter;
static uint8_t runtime_keys_mask;

enum {
	RUNTIME_KEY_VOL_UP,
	RUNTIME_KEY_LEFT_ALT,
	RUNTIME_KEY_RIGHT_ALT,
	RUNTIME_KEY_H,
	RUNTIME_KEY_R,
};

#define RUNTIME_KEY_ALL_COUNT (RUNTIME_KEY_R + 1)

/* The keys can be specified directly in the runtime-keys node or, if multiple
 * keyboard layouts are supported, in one child node per layout.
 */
#define RUNTIME_KEYS_CONFIG_COUNT DT_INST_CHILD_NUM(0)

BUILD_ASSERT(RUNTIME_KEYS_CONFIG_COUNT == 0 ||
		     !DT_INST_NODE_HAS_PROP(0, left_alt_rc),
	     "runtime-keys: specify the keys either in the node or in the "
	     "child nodes, not both");

/* vol-up-rc is only specified when not using CROS_EC_VIVALDI_KBD, otherwise
 * the placeholder is not used and the key is determined in runtime.
 */
#define RUNTIME_KEYS_INIT(node_id)                                        \
	{                                                                 \
		[RUNTIME_KEY_VOL_UP] =                                    \
			DT_PROP_OR(node_id, vol_up_rc, UINT32_MAX),       \
		[RUNTIME_KEY_LEFT_ALT] = DT_PROP(node_id, left_alt_rc),   \
		[RUNTIME_KEY_RIGHT_ALT] = DT_PROP(node_id, right_alt_rc), \
		[RUNTIME_KEY_H] = DT_PROP(node_id, h_rc),                 \
		[RUNTIME_KEY_R] = DT_PROP(node_id, r_rc),                 \
	}

#define RUNTIME_KEYS_CHILD_INIT(node_id) \
	[DT_NODE_CHILD_IDX(node_id)] = RUNTIME_KEYS_INIT(node_id),

static const uint32_t runtime_keys_configs[][RUNTIME_KEY_ALL_COUNT] = {
#if RUNTIME_KEYS_CONFIG_COUNT > 0
	DT_INST_FOREACH_CHILD(0, RUNTIME_KEYS_CHILD_INIT)
#else
	RUNTIME_KEYS_INIT(DT_DRV_INST(0)),
#endif
};

#if RUNTIME_KEYS_CONFIG_COUNT > 1
static int8_t runtime_keys_active_idx = -1;

static const uint32_t *get_runtime_keys(void)
{
	if (runtime_keys_active_idx < 0) {
		int8_t idx = board_runtime_keys_idx();

		if (idx < 0 || idx >= RUNTIME_KEYS_CONFIG_COUNT) {
			LOG_ERR("invalid runtime keys config index: %d", idx);
			idx = 0;
		}

		LOG_INF("runtime_keys: config %d", idx);
		runtime_keys_active_idx = idx;
	}

	return runtime_keys_configs[runtime_keys_active_idx];
}
#else
static const uint32_t *get_runtime_keys(void)
{
	return runtime_keys_configs[0];
}
#endif

#define REBOOT_MASK_A                                          \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_LEFT_ALT) | \
	 BIT(RUNTIME_KEY_R))
#define REBOOT_MASK_B                                           \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_RIGHT_ALT) | \
	 BIT(RUNTIME_KEY_R))
#define HIBERNATE_MASK_A                                        \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_RIGHT_ALT) | \
	 BIT(RUNTIME_KEY_H))
#define HIBERNATE_MASK_B                                       \
	(BIT(RUNTIME_KEY_VOL_UP) | BIT(RUNTIME_KEY_LEFT_ALT) | \
	 BIT(RUNTIME_KEY_H))

/* Only consider combinations of three keys (vol-up, left or right alt and a
 * letter).
 **/
#define RUNTIME_KEY_COUNT 3

#ifdef CONFIG_CROS_EC_VIVALDI_KBD
#define VOL_UP_RC_VALIDATE(node_id)                         \
	BUILD_ASSERT(!DT_NODE_HAS_PROP(node_id, vol_up_rc), \
		     "do not specify vol-up-rc not CROS_EC_VIVALDI_KBD");
#else
#define VOL_UP_RC_VALIDATE(node_id)                                \
	BUILD_ASSERT(DT_NODE_HAS_PROP(node_id, vol_up_rc),         \
		     "vol-up-rc must be specified when not using " \
		     "CROS_EC_VIVALDI_KBD");
#endif

#if RUNTIME_KEYS_CONFIG_COUNT > 0
DT_INST_FOREACH_CHILD(0, VOL_UP_RC_VALIDATE)
#else
VOL_UP_RC_VALIDATE(DT_DRV_INST(0))
#endif

static bool key_is_vol_up(const uint32_t *runtime_keys, uint8_t row,
			  uint8_t col)
{
#if defined(CONFIG_CROS_EC_VIVALDI_KBD) || (CONFIG_TEST_RUNTIME_KEYS_VIVALDI)
	return vivaldi_kbd_is_vol_up(row, col);
#else
	return KBD_RC(row, col) == runtime_keys[RUNTIME_KEY_VOL_UP];
#endif
}

static void process_key(uint8_t row, uint8_t col, bool pressed)
{
	const uint32_t *runtime_keys = get_runtime_keys();

	if (pressed) {
		runtime_keys_counter++;
	} else {
		runtime_keys_counter--;
	}

	for (uint8_t i = 0; i < RUNTIME_KEY_ALL_COUNT; i++) {
		bool key_match;

		if (i == RUNTIME_KEY_VOL_UP) {
			key_match = key_is_vol_up(runtime_keys, row, col);
		} else {
			key_match = runtime_keys[i] == KBD_RC(row, col);
		}

		if (key_match) {
			WRITE_BIT(runtime_keys_mask, i, pressed);
		}
	}

	LOG_DBG("runtime_keys: runtime_keys_mask=0x%02x counter=%d "
		"(row=%d col=%d)",
		runtime_keys_mask, runtime_keys_counter, row, col);

	if (runtime_keys_counter != RUNTIME_KEY_COUNT) {
		return;
	}

	switch (runtime_keys_mask) {
	case REBOOT_MASK_A:
	case REBOOT_MASK_B:
		LOG_DBG("runtime_keys: reboot");
		keyboard_clear_buffer();
		chipset_reset(CHIPSET_RESET_KB_WARM_REBOOT);
		break;
	case HIBERNATE_MASK_A:
	case HIBERNATE_MASK_B:
		LOG_DBG("runtime_keys: hibernate");
		system_enter_hibernate(0, 0);
		break;
	default:
		return;
	}
}

static void runtime_keys_input_cb(struct input_event *evt, void *user_data)
{
	static uint8_t row;
	static uint8_t col;
	static bool pressed;

	switch (evt->code) {
	case INPUT_ABS_X:
		col = evt->value;
		break;
	case INPUT_ABS_Y:
		row = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		pressed = evt->value;
		break;
	}

	if (!evt->sync) {
		return;
	}

	process_key(row, col, pressed);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE),
		      runtime_keys_input_cb, NULL);

#if CONFIG_TEST
void test_reinit(void)
{
	runtime_keys_counter = 0;
	runtime_keys_mask = 0;
#if RUNTIME_KEYS_CONFIG_COUNT > 1
	runtime_keys_active_idx = -1;
#endif
}
#endif
