/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>

#include <soc.h>

#define ITIM5_NODE DT_NODELABEL(itim5)

/*
 * Old FPMCU RO (EC based) uses ITIM32-5 as a watchdog warning to save the
 * CPU state before the real watchdog resets it. Zephyr instead uses the
 * watchdog module and it's capability to trigger warnings, so we disable this
 * timer to avoid an unhandled IRQ crash in cases where ECOS RO is used to
 * boot Zephyr RW.
 */
static const struct npcx_clk_cfg itim5_clk_cfg = {
	.bus = DT_CLOCKS_CELL_BY_IDX(ITIM5_NODE, 0, bus),
	.ctrl = DT_CLOCKS_CELL_BY_IDX(ITIM5_NODE, 0, ctl),
	.bit = DT_CLOCKS_CELL_BY_IDX(ITIM5_NODE, 0, bit),
};

static int disable_itim5(void)
{
	static struct itim32_reg *const itim5_reg =
		(struct itim32_reg *)DT_REG_ADDR(ITIM5_NODE);

	irq_disable(DT_IRQN(ITIM5_NODE));

	itim5_reg->ITCTS32 &= ~BIT(NPCX_ITCTSXX_ITEN);

	clock_control_off(DEVICE_DT_GET(NPCX_CLK_CTRL_NODE),
			  (clock_control_subsys_t)&itim5_clk_cfg);

	return 0;
}
SYS_INIT(disable_itim5, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
