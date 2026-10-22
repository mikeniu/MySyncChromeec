/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * 8042 keyboard Fn key handling
 */

#define DT_DRV_COMPAT cros_ec_fn_keys

#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/input/input_keymap.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/fn_keys.h>
#include <dt-bindings/kbd.h>

LOG_MODULE_REGISTER(fn_keys, LOG_LEVEL_INF);

#include "keyboard_config.h"
#include "keyboard_protocol.h"

/* The keys can be specified directly in the fn-keys node or, if multiple
 * keyboard layouts are supported, in one child node per layout.
 */
#define FN_KEYS_CONFIG_COUNT DT_INST_CHILD_NUM(0)

BUILD_ASSERT(FN_KEYS_CONFIG_COUNT == 0 || !DT_INST_NODE_HAS_PROP(0, fn_rc),
	     "fn-keys: specify the keys either in the node or in the child "
	     "nodes, not both");

struct fn_keys_config {
	uint16_t fn_key_rc;
	uint8_t keymap_len;
	const uint32_t *keymap;
};

#define FN_KEYMAP_NAME(node_id) UTIL_CAT(fn_keymap_, node_id)

/* fn_keys_status tracks one bit per key */
#define FN_KEYMAP_DEFINE(node_id)                             \
	BUILD_ASSERT(DT_PROP_LEN(node_id, keymap) <= 32,      \
		     "fn-keys: up to 32 keys are supported"); \
	static const uint32_t FN_KEYMAP_NAME(node_id)[] =     \
		DT_PROP(node_id, keymap);

#define FN_KEYS_INIT(node_id)                               \
	{                                                   \
		.fn_key_rc = DT_PROP(node_id, fn_rc),       \
		.keymap_len = DT_PROP_LEN(node_id, keymap), \
		.keymap = FN_KEYMAP_NAME(node_id),          \
	}

#define FN_KEYS_CHILD_INIT(node_id) \
	[DT_NODE_CHILD_IDX(node_id)] = FN_KEYS_INIT(node_id),

#if FN_KEYS_CONFIG_COUNT > 0
DT_INST_FOREACH_CHILD(0, FN_KEYMAP_DEFINE)

static const struct fn_keys_config fn_keys_configs[] = { DT_INST_FOREACH_CHILD(
	0, FN_KEYS_CHILD_INIT) };
#else
FN_KEYMAP_DEFINE(DT_DRV_INST(0))

static const struct fn_keys_config fn_keys_configs[] = {
	FN_KEYS_INIT(DT_DRV_INST(0)),
};
#endif

#if FN_KEYS_CONFIG_COUNT > 1
static int8_t fn_keys_active_idx = -1;

static const struct fn_keys_config *get_fn_keys(void)
{
	if (fn_keys_active_idx < 0) {
		int8_t idx = board_fn_keys_idx();

		if (idx < 0 || idx >= FN_KEYS_CONFIG_COUNT) {
			LOG_ERR("invalid fn keys config index: %d", idx);
			idx = 0;
		}

		LOG_INF("fn_keys: config %d", idx);
		fn_keys_active_idx = idx;
	}

	return &fn_keys_configs[fn_keys_active_idx];
}
#else
static const struct fn_keys_config *get_fn_keys(void)
{
	return &fn_keys_configs[0];
}
#endif

static bool fn_key_pressed;
static bool fn_key_triggered;
static uint32_t fn_keys_status;
static uint8_t normal_keys_status[KEYBOARD_COLS_MAX];

static bool is_key(int row, int col, uint32_t rc)
{
	if (row == MATRIX_ROW(rc) && col == MATRIX_COL(rc)) {
		return true;
	}

	return false;
}

void keyboard_state_changed(int row, int col, int is_pressed)
{
	const struct fn_keys_config *cfg = get_fn_keys();
	const uint32_t *fn_keys = cfg->keymap;

	if (KBD_RC(row, col) == cfg->fn_key_rc) {
		fn_key_pressed = is_pressed;

		if (is_pressed) {
			fn_key_triggered = false;
		} else if (!fn_key_triggered) {
			/* trigger a press and release of the Fn key if nothing
			 * else has been pressed
			 */
			keyboard_state_changed_process(row, col, true, -1);
			keyboard_state_changed_process(row, col, false, -1);
		}

		return;
	}

	if (!is_pressed) {
		/* Handle release regardless of Fn status */
		for (uint8_t i = 0; i < cfg->keymap_len; i++) {
			int override_code;

			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			if ((fn_keys_status & BIT(i)) == 0) {
				continue;
			}

			override_code = MATRIX_CODE(fn_keys[i]);
			fn_keys_status &= ~BIT(i);

			keyboard_state_changed_process(row, col, is_pressed,
						       override_code);

			return;
		}

		if ((normal_keys_status[col] & BIT(row)) == 0) {
			/* Discard, key press code was not sent */
			return;
		}
	} else if (fn_key_pressed) {
		/* Handle press while holding Fn */
		fn_key_triggered = true;

		for (uint8_t i = 0; i < cfg->keymap_len; i++) {
			int override_code;

			if (!is_key(row, col, fn_keys[i])) {
				continue;
			}

			override_code = MATRIX_CODE(fn_keys[i]);
			fn_keys_status |= BIT(i);

			keyboard_state_changed_process(row, col, is_pressed,
						       override_code);

			return;
		}

		/* Do not emit a code if the key is not mapped */
		return;
	}

	if (is_pressed) {
		normal_keys_status[col] |= BIT(row);
	} else {
		normal_keys_status[col] &= ~BIT(row);
	}

	keyboard_state_changed_process(row, col, is_pressed, -1);
}
