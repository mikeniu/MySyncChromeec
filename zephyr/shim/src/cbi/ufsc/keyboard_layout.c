/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "drivers/vivaldi_kbd.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_config.h"
#include "keyboard_scan.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT cros_ec_cbi_ufsc_keyboard_selector

LOG_MODULE_REGISTER(cros_cbi_ufsc_keyboard, LOG_LEVEL_INF);

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0

#define SELECTOR_NODE DT_DRV_INST(0)

test_export_static int8_t active_vivaldi_idx = -1;

#define APPLY_PATCH_FN(node_id) apply_patch_##node_id
#define CALL_APPLY_PATCH(node_id) APPLY_PATCH_FN(node_id)()

/* Generate patch applicator functions for each keyboard-layout-patch DT node.
 */
#define GENERATE_PATCH_APPLY(patch_node)                                           \
	static void __maybe_unused APPLY_PATCH_FN(patch_node)(void)                \
	{                                                                          \
		COND_CODE_1(                                                       \
			DT_NODE_HAS_PROP(patch_node, scan_cols),                   \
			(keyboard_set_cols(DT_PROP(patch_node, scan_cols));),      \
			())                                                        \
		COND_CODE_1(                                                       \
			DT_NODE_HAS_PROP(patch_node, vol_up_rc),                   \
			(set_vol_up_key(                                           \
				 DT_PROP_BY_IDX(patch_node, vol_up_rc, 0),         \
				 DT_PROP_BY_IDX(patch_node, vol_up_rc, 1));),      \
			())                                                        \
		COND_CODE_1(                                                       \
			DT_NODE_HAS_PROP(patch_node, vivaldi_config),              \
			(active_vivaldi_idx = DT_NODE_CHILD_IDX(                   \
				 DT_PHANDLE(patch_node, vivaldi_config));),        \
			())                                                        \
		IF_ENABLED(                                                        \
			DT_NODE_HAS_PROP(patch_node, swaps), ({                    \
				static const uint8_t s[] =                         \
					DT_PROP(patch_node, swaps);                \
				for (size_t i = 0; i < sizeof(s); i += 4) {        \
					set_scancode_set2(                         \
						s[i], s[i + 1],                    \
						get_scancode_set2(s[i + 2],        \
								  s[i + 3]));      \
					IF_ENABLED(                                \
						CONFIG_PLATFORM_EC_KEYBOARD_DEBUG, \
						(set_keycap_label(                 \
							 s[i], s[i + 1],           \
							 get_keycap_label(         \
								 s[i + 2],         \
								 s[i + 3]));))     \
				}                                                  \
			}))                                                        \
		IF_ENABLED(DT_NODE_HAS_PROP(patch_node, patches), ({               \
				   static const uint16_t p[] =                     \
					   DT_PROP(patch_node, patches);           \
				   for (size_t i = 0; i < ARRAY_SIZE(p);           \
					i += 3) {                                  \
					   set_scancode_set2(                      \
						   (uint8_t)p[i],                  \
						   (uint8_t)p[i + 1],              \
						   p[i + 2]);                      \
				   }                                               \
			   }))                                                     \
	}

DT_FOREACH_STATUS_OKAY(cros_ec_keyboard_layout_patch, GENERATE_PATCH_APPLY)

/*
 * Step through alternating pairs of <&ufsc_value &patch_node> in each category.
 * If the UFSC value matches, invoke the patch applicator for the following
 * node.
 */
#define CHECK_AND_APPLY_MATCH(node_id, prop, idx, matched_ptr)                \
	COND_CODE_1(DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node_id, prop, idx), \
				       cros_ec_cbi_ufsc_value),               \
		    (if (!*(matched_ptr) &&                                   \
			 cros_cbi_ufsc_check_match(CBI_UFSC_VALUE_ID(         \
				 DT_PHANDLE_BY_IDX(node_id, prop, idx)))) {   \
			    *(matched_ptr) = true;                            \
			    CALL_APPLY_PATCH(DT_PHANDLE_BY_IDX(               \
				    node_id, prop, UTIL_INC(idx)));           \
		    }),                                                       \
		    ())

#define PROCESS_SELECTOR_STEP(child_node)                                    \
	do {                                                                 \
		bool matched = false;                                        \
		DT_FOREACH_PROP_ELEM_VARGS(child_node, selections,           \
					   CHECK_AND_APPLY_MATCH, &matched); \
	} while (0);

test_export_static void keyboard_layout_ufsc_init(void)
{
	DT_FOREACH_CHILD(SELECTOR_NODE, PROCESS_SELECTOR_STEP);
}
DECLARE_HOOK(HOOK_INIT, keyboard_layout_ufsc_init, HOOK_PRIO_POST_I2C);

__override int8_t board_vivaldi_keybd_idx(void)
{
	if (active_vivaldi_idx < 0) {
		keyboard_layout_ufsc_init();
	}
	return active_vivaldi_idx >= 0 ? active_vivaldi_idx : 0;
}

#endif /* DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0 */
