/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "driver/bc12/rt1718s.h"
#include "driver/tcpm/rt1718s.h"
#include "emul/emul_common_i2c.h"
#include "emul/tcpc/emul_rt1718s.h"
#include "emul/tcpc/emul_tcpci.h"
#include "test/drivers/test_state.h"
#include "test_common.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

static void rt1718s_bc12_after(void *f)
{
	charge_manager_set_override(OVERRIDE_OFF);
	rt1718s_clear_set_reg_history(f);
}

ZTEST_SUITE(rt1718s_bc12, drivers_predicate_post_main, NULL,
	    rt1718s_clear_set_reg_history, rt1718s_bc12_after, NULL);

ZTEST(rt1718s_bc12, test_bc12_task_init)
{
	/* Set sink enable bit first to verify task_init clears it */
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC,
					RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN));

	rt1718s_bc12_drv.usb_charger_task_init(tcpm_rt1718s_port);

	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC, 0,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);
}

ZTEST(rt1718s_bc12, test_bc12_direct_init)
{
	zassert_ok(rt1718s_bc12_init(tcpm_rt1718s_port));

	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT_MASK6,
				  RT1718S_RT_MASK6_M_BC12_SNK_DONE |
					  RT1718S_RT_MASK6_M_BC12_TA_CHG,
				  0xFF);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_SBU_CTRL_01,
				  RT1718S_RT2_SBU_CTRL_01_DPDM_VIEN |
					  RT1718S_RT2_SBU_CTRL_01_DM_SWEN |
					  RT1718S_RT2_SBU_CTRL_01_DP_SWEN,
				  0xFF);
	compare_reg_val_with_mask(
		rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC,
		RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_600MS,
		RT1718S_RT2_BC12_SNK_FUNC_SPEC_TA_EN |
			RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_MASK |
			RT1718S_RT2_BC12_SNK_FUNC_VLGC_OPT |
			RT1718S_RT2_BC12_SNK_FUNC_BC12_WAIT_VBUS);
	compare_reg_val_with_mask(
		rt1718s_emul, RT1718S_RT2_DPDM_CTR1_DPDM_SET,
		RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_65V,
		RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_MASK);
}

ZTEST(rt1718s_bc12, test_bc12_task_event)
{
	struct tcpc_emul_data *tcpc_data = rt1718s_emul->data;

	/* Configure VBUS detection to TCPC */
	set_usb_pd_vbus_detect(USB_PD_VBUS_DETECT_TCPC);

	/* Leave safe mode and seed charge manager */
	charge_manager_leave_safe_mode();
	for (int p = 0; p < CHARGE_PORT_COUNT; p++) {
		for (int s = 0; s < CHARGE_SUPPLIER_COUNT; s++) {
			charge_manager_update_charge(s, p, NULL);
		}
	}
	charge_manager_update_dualrole(tcpm_rt1718s_port, CAP_DEDICATED);
	k_sleep(K_MSEC(50));

	/* Set VBUS present */
	zassert_ok(tcpci_emul_set_vbus_level(rt1718s_emul, VBUS_PRESENT));
	zassert_ok(rt1718s_tcpm_drv.init(tcpm_rt1718s_port));
	k_sleep(K_MSEC(50));
	charge_manager_update_dualrole(tcpm_rt1718s_port, CAP_DEDICATED);
	zassert_ok(charge_manager_set_override(tcpm_rt1718s_port));
	zassert_true(pd_check_vbus_level(tcpm_rt1718s_port, VBUS_PRESENT));
	zassert_false(pd_capable(tcpm_rt1718s_port));
	zassert_false(usb_charger_port_is_sourcing_vbus(tcpm_rt1718s_port));

	/* USB_CHG_EVENT_VBUS enables BC1.2 sink */
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_VBUS);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);

	/* 1. Test SDP detection */
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_STAT,
					RT1718S_RT2_BC12_STAT_PORT_STATUS_SDP));
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC, 0,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_BC12_SDP);
	zassert_equal(charge_manager_get_charger_current(),
		      USB_CHARGER_MIN_CURR_MA);

	/* Repeating SDP covers new_bc12_type == current_bc12_type branch */
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_BC12_SDP);

	/* 2. Test CDP detection (and transition from non-NONE supplier) */
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_STAT,
					RT1718S_RT2_BC12_STAT_PORT_STATUS_CDP));
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC, 0,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_BC12_CDP);
	zassert_equal(charge_manager_get_charger_current(),
		      USB_CHARGER_MAX_CURR_MA);

	/* 3. Test DCP detection */
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_STAT,
					RT1718S_RT2_BC12_STAT_PORT_STATUS_DCP));
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC, 0,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_BC12_DCP);
	zassert_equal(charge_manager_get_charger_current(),
		      USB_CHARGER_MAX_CURR_MA);

	/* 4. Test Special TA 0.5A/1A (0x01) -> CHARGE_SUPPLIER_OTHER */
	charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, tcpm_rt1718s_port,
				     NULL);
	zassert_ok(rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_STAT,
					0x01));
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	compare_reg_val_with_mask(rt1718s_emul, RT1718S_RT2_BC12_SNK_FUNC, 0,
				  RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_OTHER);
	zassert_equal(charge_manager_get_charger_current(),
		      USB_CHARGER_MIN_CURR_MA);

	/* 5. Test PORT_STATUS_NONE (0x00) -> CHARGE_SUPPLIER_NONE */
	zassert_ok(
		rt1718s_emul_set_reg(rt1718s_emul, RT1718S_RT2_BC12_STAT,
				     RT1718S_RT2_BC12_STAT_PORT_STATUS_NONE));
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_NONE);

	/* 6. Test I2C read failure on RT1718S_RT2_BC12_STAT -> OTHER */
	charge_manager_update_charge(CHARGE_SUPPLIER_VBUS, tcpm_rt1718s_port,
				     NULL);
	i2c_common_emul_set_read_fail_reg(&tcpc_data->tcpci_ctx->common,
					  RT1718S_RT2_BC12_STAT & 0xFF);
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	i2c_common_emul_set_read_fail_reg(&tcpc_data->tcpci_ctx->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	k_msleep(500);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_OTHER);

	/* 7. Test non-PD-sink false path (VBUS_REMOVED) */
	zassert_ok(tcpci_emul_set_vbus_level(rt1718s_emul, VBUS_REMOVED));
	zassert_ok(rt1718s_tcpm_drv.init(tcpm_rt1718s_port));
	k_msleep(50);
	zassert_false(pd_check_vbus_level(tcpm_rt1718s_port, VBUS_PRESENT));

	/* When VBUS is removed, BC12 event sets type to NONE */
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_BC12);
	k_msleep(500);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_NONE);

	/* When VBUS is removed, VBUS event updates manager to NONE */
	rt1718s_bc12_drv.usb_charger_task_event(tcpm_rt1718s_port,
						USB_CHG_EVENT_VBUS);
	k_msleep(500);
	zassert_equal(charge_manager_get_supplier(), CHARGE_SUPPLIER_NONE);
}
