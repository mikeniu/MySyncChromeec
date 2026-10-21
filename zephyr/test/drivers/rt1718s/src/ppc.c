/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/ppc/rt1718s.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpci.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "test/drivers/test_state.h"
#include "test_common.h"
#include "usbc_ppc.h"

#include <zephyr/ztest.h>

static struct ppc_config_t saved_ppc_chip;

static void rt1718s_ppc_before(void *fixture)
{
	saved_ppc_chip = ppc_chips[tcpm_rt1718s_port];
	ppc_chips[tcpm_rt1718s_port].drv = &rt1718s_ppc_drv;
	ppc_chips[tcpm_rt1718s_port].i2c_port =
		tcpc_config[tcpm_rt1718s_port].i2c_info.port;
	ppc_chips[tcpm_rt1718s_port].i2c_addr_flags =
		tcpc_config[tcpm_rt1718s_port].i2c_info.addr_flags;

	zassert_ok(rt1718s_tcpm_drv.init(tcpm_rt1718s_port));
	rt1718s_clear_set_reg_history(fixture);
}

static void rt1718s_ppc_after(void *fixture)
{
	ppc_chips[tcpm_rt1718s_port] = saved_ppc_chip;
	rt1718s_clear_set_reg_history(fixture);
}

ZTEST_SUITE(rt1718s_ppc, drivers_predicate_post_main, NULL, rt1718s_ppc_before,
	    rt1718s_ppc_after, NULL);

ZTEST(rt1718s_ppc, test_ppc_init)
{
	zassert_ok(ppc_init(tcpm_rt1718s_port));
	zassert_equal(ppc_is_sourcing_vbus(tcpm_rt1718s_port), 0);
}

ZTEST(rt1718s_ppc, test_ppc_vbus_source_sink)
{
	zassert_ok(ppc_vbus_source_enable(tcpm_rt1718s_port, 1));
	zassert_equal(ppc_is_sourcing_vbus(tcpm_rt1718s_port), 1);
	compare_reg_val_with_mask(
		rt1718s_emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_HIGH,
		TCPC_REG_COMMAND_SRC_CTRL_HIGH | TCPC_REG_COMMAND_SRC_CTRL_LOW);

	zassert_ok(ppc_vbus_source_enable(tcpm_rt1718s_port, 0));
	zassert_equal(ppc_is_sourcing_vbus(tcpm_rt1718s_port), 0);
	compare_reg_val_with_mask(
		rt1718s_emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SRC_CTRL_LOW,
		TCPC_REG_COMMAND_SRC_CTRL_HIGH | TCPC_REG_COMMAND_SRC_CTRL_LOW);

	zassert_ok(ppc_vbus_sink_enable(tcpm_rt1718s_port, 1));
	compare_reg_val_with_mask(
		rt1718s_emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_HIGH,
		TCPC_REG_COMMAND_SNK_CTRL_HIGH | TCPC_REG_COMMAND_SNK_CTRL_LOW);

	zassert_ok(ppc_vbus_sink_enable(tcpm_rt1718s_port, 0));
	compare_reg_val_with_mask(
		rt1718s_emul, TCPC_REG_COMMAND, TCPC_REG_COMMAND_SNK_CTRL_LOW,
		TCPC_REG_COMMAND_SNK_CTRL_HIGH | TCPC_REG_COMMAND_SNK_CTRL_LOW);
}

ZTEST(rt1718s_ppc, test_ppc_discharge_vbus)
{
	uint16_t val;

	zassert_ok(ppc_discharge_vbus(tcpm_rt1718s_port, 1));
	zassert_ok(
		rt1718s_emul_get_reg(rt1718s_emul, TCPC_REG_POWER_CTRL, &val));
	zassert_true(val & TCPC_REG_POWER_CTRL_FORCE_DISCHARGE);

	zassert_ok(ppc_discharge_vbus(tcpm_rt1718s_port, 0));
	zassert_ok(
		rt1718s_emul_get_reg(rt1718s_emul, TCPC_REG_POWER_CTRL, &val));
	zassert_false(val & TCPC_REG_POWER_CTRL_FORCE_DISCHARGE);
}

ZTEST(rt1718s_ppc, test_ppc_polarity)
{
	zassert_ok(ppc_set_polarity(tcpm_rt1718s_port, 0));
	compare_reg_val_with_mask(rt1718s_emul, TCPC_REG_TCPC_CTRL, 0,
				  TCPC_REG_TCPC_CTRL_SET(1));

	zassert_ok(ppc_set_polarity(tcpm_rt1718s_port, 1));
	compare_reg_val_with_mask(rt1718s_emul, TCPC_REG_TCPC_CTRL,
				  TCPC_REG_TCPC_CTRL_SET(1),
				  TCPC_REG_TCPC_CTRL_SET(1));
}

ZTEST(rt1718s_ppc, test_ppc_dump)
{
	zassert_not_null(rt1718s_ppc_drv.reg_dump);
	zassert_ok(rt1718s_ppc_drv.reg_dump(tcpm_rt1718s_port));
}
