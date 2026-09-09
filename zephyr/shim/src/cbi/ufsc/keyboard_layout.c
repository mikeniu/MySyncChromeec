/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_cbi.h"
#include "drivers/vivaldi_kbd.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#define DT_DRV_COMPAT cros_ec_cbi_ufsc_keyboard_selector

LOG_MODULE_REGISTER(cros_cbi_ufsc_keyboard, LOG_LEVEL_INF);

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0

#define SELECTOR_NODE DT_DRV_INST(0)

test_export_static int8_t active_vivaldi_idx = -1;
test_export_static bool layout_initialized;

/*
 * Apply a "copies" list: pairs of <dst_row dst_col>, <src_row src_col>.
 *
 * The source keeps its own scancode, so this is a one way copy rather than an
 * exchange. Entries are applied in order and later entries observe the writes
 * made by earlier ones, which lets a board rotate several positions by
 * chaining them.
 */
static void __maybe_unused apply_copies(const uint8_t *rc, size_t len)
{
	for (size_t i = 0; i < len; i += 4) {
		const uint8_t dst_row = rc[i];
		const uint8_t dst_col = rc[i + 1];
		const uint8_t src_row = rc[i + 2];
		const uint8_t src_col = rc[i + 3];

		set_scancode_set2(dst_row, dst_col,
				  get_scancode_set2(src_row, src_col));

		/*
		 * Must be #ifdef rather than IS_ENABLED(): the keycap label
		 * accessors are only declared when CONFIG_KEYBOARD_DEBUG is
		 * set, and IS_ENABLED() would still compile the body.
		 */
#ifdef CONFIG_KEYBOARD_DEBUG
		set_keycap_label(dst_row, dst_col,
				 get_keycap_label(src_row, src_col));
#endif
	}
}

#define APPLY_PATCH_FN(node_id) apply_patch_##node_id
#define CALL_APPLY_PATCH(node_id) APPLY_PATCH_FN(node_id)()

/*
 * Generate one applicator function per cros-ec,keyboard-layout-patch node.
 *
 * For example this node:
 *
 *	patch_us2: patch-us2 {
 *		compatible = "cros-ec,keyboard-layout-patch";
 *		copies = <3 14>, <2 7>;
 *	};
 *
 * generates:
 *
 *	static void apply_patch_patch_us2(void)
 *	{
 *		static const uint8_t rc[] = { 3, 14, 2, 7 };
 *
 *		apply_copies(rc, ARRAY_SIZE(rc));
 *	}
 */
#define GENERATE_PATCH_APPLY(patch_node)                                    \
	static void __maybe_unused APPLY_PATCH_FN(patch_node)(void)         \
	{                                                                   \
		LOG_INF("keyboard layout: applying %s",                     \
			DT_NODE_FULL_NAME(patch_node));                     \
		IF_ENABLED(DT_NODE_HAS_PROP(patch_node, copies), ({         \
				   static const uint8_t rc[] =              \
					   DT_PROP(patch_node, copies);     \
				   BUILD_ASSERT(ARRAY_SIZE(rc) % 4 == 0,    \
						"copies must be pairs of "  \
						"<dst_row dst_col>, "       \
						"<src_row src_col>");       \
				   apply_copies(rc, ARRAY_SIZE(rc));        \
			   }))                                              \
		COND_CODE_1(                                                \
			DT_NODE_HAS_PROP(patch_node, vivaldi_config),       \
			(active_vivaldi_idx = DT_NODE_CHILD_IDX(            \
				 DT_PHANDLE(patch_node, vivaldi_config));), \
			())                                                 \
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
	layout_initialized = true;
}
DECLARE_HOOK(HOOK_INIT, keyboard_layout_ufsc_init, HOOK_PRIO_POST_I2C);

__override int8_t board_vivaldi_keybd_idx(void)
{
	/*
	 * The Vivaldi driver initializes at HOOK_PRIO_DEFAULT, well after this
	 * driver's HOOK_PRIO_POST_I2C, but it may also be queried before
	 * HOOK_INIT runs at all. Initialize on demand in that case.
	 *
	 * Track this with a separate flag rather than the value of
	 * active_vivaldi_idx, which stays at -1 on the many boards that only
	 * declare copies and never select a Vivaldi config.
	 */
	if (!layout_initialized) {
		keyboard_layout_ufsc_init();
	}
	return active_vivaldi_idx >= 0 ? active_vivaldi_idx : 0;
}

#endif /* DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0 */
