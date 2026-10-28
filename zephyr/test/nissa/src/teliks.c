/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"
#include "teliks.h"
#include "teliks_sub_board.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <dt-bindings/gpio_defines.h>

LOG_MODULE_REGISTER(nissa, LOG_LEVEL_INF);

extern enum teliks_sub_board_type teliks_cached_sub_board;
void board_usb_pd_count_init(void);

FAKE_VALUE_FUNC(int, cbi_get_ssfc, uint32_t *);
FAKE_VALUE_FUNC(int, cros_cbi_get_fw_config, enum cbi_fw_config_field_id,
		uint32_t *);
FAKE_VOID_FUNC(bmi3xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lsm6dsm_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(icm42607_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(bma4xx_interrupt, enum gpio_signal);
FAKE_VOID_FUNC(lis2dw12_interrupt, enum gpio_signal);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_is_vbus_present, int);
FAKE_VALUE_FUNC(int, ppc_set_vbus_source_current_limit, int,
		enum tcpc_rp_value);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);

static bool clamshell_mode;

static int cbi_get_form_factor_config(enum cbi_fw_config_field_id field,
				      uint32_t *value)
{
	if (field == FORM_FACTOR)
		*value = clamshell_mode ? CLAMSHELL : CONVERTIBLE;
	return 0;
}

static int cbi_get_form_factor_config_error(enum cbi_fw_config_field_id field,
					    uint32_t *value)
{
	return -1;
}

ZTEST(teliks, test_board_setup_init_clamshell)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);
	int interrupt_count;

	/* Clear base and lid sensor interrupt call count before test */
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);

	/* CBI config error */
	cros_cbi_get_fw_config_fake.custom_fake =
		cbi_get_form_factor_config_error;
	board_setup_init();
	alt_sensor_init();

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	clamshell_mode = true;
	board_setup_init();
	/* Clamshel mode */
	alt_sensor_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);

	/* Verify base and lid sensor interrupt is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dsm_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count +
			  bma4xx_interrupt_fake.call_count +
			  lis2dw12_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 0);
}

static int ssfc_data;

static int cbi_get_ssfc_mock(uint32_t *ssfc)
{
	*ssfc = ssfc_data;
	return 0;
}

ZTEST(teliks, test_board_setup_init_convertible)
{
	const struct device *tablet_mode_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_tablet_mode_l), gpios));
	const gpio_port_pins_t tablet_mode_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_tablet_mode_l), gpios);
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);
	int interrupt_count;

	/* Clear base and lid sensor interrupt call count before test */
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	/* Initial ssfc data for BMA422 and BMI323. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x9;
	cros_cbi_ssfc_init();

	/* reset tablet mode for initialize status.
	 * enable int_imu and int_tablet_mode before clashell_init
	 * for the priorities of sensor_enable_irqs and
	 * gmr_tablet_switch_init is earlier.
	 */
	tablet_reset();
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_tablet_mode));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;
	clamshell_mode = false;
	board_setup_init();

	/* Verify gmr_tablet_switch is disabled, by checking the side effects
	 * of calling tablet_set_mode, and setting gpio_tablet_mode_l.
	 */
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 1),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	zassert_equal(0, tablet_get_mode(), NULL);
	zassert_ok(gpio_emul_input_set(tablet_mode_gpio, tablet_mode_pin, 0),
		   NULL);
	k_sleep(K_MSEC(100));
	tablet_set_mode(1, TABLET_TRIGGER_LID);
	zassert_equal(1, tablet_get_mode(), NULL);

	/* Verify base and lid sensor interrupt is disabled. */
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	interrupt_count = bmi3xx_interrupt_fake.call_count +
			  lsm6dsm_interrupt_fake.call_count +
			  icm42607_interrupt_fake.call_count +
			  bma4xx_interrupt_fake.call_count +
			  lis2dw12_interrupt_fake.call_count;
	zassert_equal(interrupt_count, 2);

	zassert_equal(bmi3xx_interrupt_fake.call_count, 1);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);
	zassert_equal(icm42607_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 1);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 0);
}

ZTEST(teliks, test_alt_sensor)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Clear base and lid sensor interrupt call count before test */
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	/* Initial ssfc data for LSM6DSM and LIS2DW. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x12;
	cros_cbi_ssfc_init();

	/* Enable the interrupt int_imu and int_lid_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 1);
	zassert_equal(icm42607_interrupt_fake.call_count, 0);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

ZTEST(teliks, test_alt_sensor_icm42607)
{
	const struct device *base_imu_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_imu_int_l), gpios));
	const gpio_port_pins_t base_imu_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_imu_int_l), gpios);
	const struct device *lid_accel_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_acc_int_l), gpios));
	const gpio_port_pins_t lid_accel_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_acc_int_l), gpios);

	/* Clear base and lid sensor interrupt call count before test */
	RESET_FAKE(bmi3xx_interrupt);
	RESET_FAKE(lsm6dsm_interrupt);
	RESET_FAKE(icm42607_interrupt);
	RESET_FAKE(bma4xx_interrupt);
	RESET_FAKE(lis2dw12_interrupt);

	cros_cbi_get_fw_config_fake.custom_fake = cbi_get_form_factor_config;

	/* Initial ssfc data for ICM42607 and LIS2DW. */
	cbi_get_ssfc_fake.custom_fake = cbi_get_ssfc_mock;
	ssfc_data = 0x13;
	cros_cbi_ssfc_init();

	/* Enable the interrupt int_imu and int_lid_imu */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));

	alt_sensor_init();

	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(base_imu_gpio, base_imu_pin, 0), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 1), NULL);
	k_sleep(K_MSEC(100));
	zassert_ok(gpio_emul_input_set(lid_accel_gpio, lid_accel_pin, 0), NULL);
	k_sleep(K_MSEC(100));

	zassert_equal(bmi3xx_interrupt_fake.call_count, 0);
	zassert_equal(lsm6dsm_interrupt_fake.call_count, 0);
	zassert_equal(icm42607_interrupt_fake.call_count, 1);
	zassert_equal(bma4xx_interrupt_fake.call_count, 0);
	zassert_equal(lis2dw12_interrupt_fake.call_count, 1);
}

ZTEST(teliks, test_battery_hw_present)
{
	const struct device *batt_pres_gpio = DEVICE_DT_GET(
		DT_GPIO_CTLR(DT_NODELABEL(gpio_ec_battery_pres_odl), gpios));
	const gpio_port_pins_t batt_pres_pin =
		DT_GPIO_PIN(DT_NODELABEL(gpio_ec_battery_pres_odl), gpios);

	zassert_not_null(batt_pres_gpio, NULL);

	zassert_ok(gpio_emul_input_set(batt_pres_gpio, batt_pres_pin, 0));
	zassert_equal(BP_YES, battery_hw_present());

	zassert_ok(gpio_emul_input_set(batt_pres_gpio, batt_pres_pin, 1));
	zassert_equal(BP_NO, battery_hw_present());
}

ZTEST(teliks, test_get_scancode_set2)
{
	/* Test some special keys of the customization matrix */
	zassert_equal(get_scancode_set2(6, 13), SCANCODE_LEFT_ALT);
	zassert_equal(get_scancode_set2(1, 14), SCANCODE_LEFT_CTRL);

	/* Test out of the matrix range */
	zassert_equal(get_scancode_set2(8, 12), 0);
	zassert_equal(get_scancode_set2(0, 18), 0);
}

ZTEST(teliks, test_set_scancode_set2)
{
	/* Set some special keys and read back */
	zassert_equal(get_scancode_set2(1, 0), 0);
	set_scancode_set2(1, 0, SCANCODE_LEFT_WIN);
	zassert_equal(get_scancode_set2(1, 0), SCANCODE_LEFT_WIN);

	zassert_equal(get_scancode_set2(4, 0), 0);
	set_scancode_set2(4, 0, SCANCODE_CAPSLOCK);
	zassert_equal(get_scancode_set2(4, 0), SCANCODE_CAPSLOCK);

	zassert_equal(get_scancode_set2(0, 13), 0);
	set_scancode_set2(0, 13, SCANCODE_F15);
	zassert_equal(get_scancode_set2(0, 13), SCANCODE_F15);
}

ZTEST(teliks, test_get_keycap_label)
{
	zassert_equal(get_keycap_label(3, 0), KLLI_SEARC);
	zassert_equal(get_keycap_label(0, 4), KLLI_F10);
	zassert_equal(get_keycap_label(8, 12), KLLI_UNKNO);
	zassert_equal(get_keycap_label(0, 18), KLLI_UNKNO);
}

ZTEST(teliks, test_set_keycap_label)
{
	zassert_equal(get_keycap_label(2, 0), KLLI_UNKNO);
	set_keycap_label(2, 0, KLLI_SEARC);
	zassert_equal(get_keycap_label(2, 0), KLLI_SEARC);

	zassert_equal(get_keycap_label(0, 14), KLLI_UNKNO);
	set_keycap_label(0, 14, KLLI_F15);
	zassert_equal(get_keycap_label(0, 14), KLLI_F15);
}

static uint32_t sb_fw_config_value;

static int get_fake_sb_fw_config(enum cbi_fw_config_field_id field_id,
				 uint32_t *value)
{
	if (field_id == FW_SUB_BOARD)
		*value = sb_fw_config_value;
	return 0;
}

ZTEST(teliks, test_sub_board)
{
	const struct gpio_dt_spec *s5_gpio =
		GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_s5);

	/* CBI read error falls back to TELIKS_SB_NONE (1 port) */
	teliks_cached_sub_board = TELIKS_SB_UNKNOWN;
	cros_cbi_get_fw_config_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(teliks_get_sb_type(), TELIKS_SB_NONE);
	/* Cached value returned on subsequent call */
	zassert_equal(teliks_get_sb_type(), TELIKS_SB_NONE);

	/* FW_SUB_BOARD_1 -> TELIKS_SB_C (2 ports) */
	teliks_cached_sub_board = TELIKS_SB_UNKNOWN;
	RESET_FAKE(cros_cbi_get_fw_config);
	cros_cbi_get_fw_config_fake.custom_fake = get_fake_sb_fw_config;
	sb_fw_config_value = FW_SUB_BOARD_1;
	gpio_pin_set_dt(s5_gpio, 0);
	hook_notify(HOOK_INIT);
	zassert_equal(teliks_get_sb_type(), TELIKS_SB_C);
	zassert_equal(board_get_usb_pd_port_count(), 2);
	zassert_equal(gpio_emul_output_get(s5_gpio->port, s5_gpio->pin), 1);

	/* Unset / default sub-board -> TELIKS_SB_NONE (1 port) */
	teliks_cached_sub_board = TELIKS_SB_UNKNOWN;
	sb_fw_config_value = 0;
	hook_notify(HOOK_INIT);
	zassert_equal(teliks_get_sb_type(), TELIKS_SB_NONE);
	zassert_equal(board_get_usb_pd_port_count(), 1);
}

ZTEST(teliks, test_usbc_1_port_sku)
{
	teliks_cached_sub_board = TELIKS_SB_NONE;
	board_usb_pd_count_init();
	zassert_equal(board_get_usb_pd_port_count(), 1);

	/* CHARGE_PORT_NONE must only disable port 0, never unpopulated port 1
	 */
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 0);

	/* Selecting port 0 must not attempt to disable unpopulated port 1 */
	RESET_FAKE(ppc_vbus_sink_enable);
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 1);

	/* Port 1 is invalid on 1-port SKU */
	RESET_FAKE(ppc_vbus_sink_enable);
	zassert_equal(board_set_active_charge_port(1), EC_ERROR_INVAL);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 0);

	zassert_equal(pd_set_power_supply_ready(1), EC_ERROR_INVAL);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 0);

	typec_set_source_current_limit(1, TYPEC_RP_3A0);
	typec_set_source_current_limit(-1, TYPEC_RP_3A0);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 0);
}

ZTEST(teliks, test_usbc_2_port_sku)
{
	teliks_cached_sub_board = TELIKS_SB_C;
	board_usb_pd_count_init();
	zassert_equal(board_get_usb_pd_port_count(), 2);

	board_pd_vconn_ctrl(0, USBPD_CC_PIN_1, 1);
	board_reset_pd_mcu();

	ppc_is_sourcing_vbus_fake.return_val = 1;
	zassert_equal(board_is_sourcing_vbus(0), 1);
	zassert_equal(board_vbus_source_enabled(0), 1);
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_INVAL);
	ppc_is_sourcing_vbus_fake.return_val = 0;

	ppc_is_vbus_present_fake.return_val = 1;
	zassert_equal(pd_snk_is_vbus_provided(0), 1);

	/* CHARGE_PORT_NONE disables both ports even if ppc_vbus_sink_enable
	 * fails */
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_ok(board_set_active_charge_port(CHARGE_PORT_NONE));
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 2);

	/* Invalid port */
	zassert_equal(board_set_active_charge_port(2), EC_ERROR_INVAL);

	/* Enable port 0 when disabling port 1 fails, and enabling port 0 fails
	 */
	RESET_FAKE(ppc_vbus_sink_enable);
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(board_set_active_charge_port(0), EC_ERROR_UNKNOWN);
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 2);

	/* Enable port 0 succeeds */
	RESET_FAKE(ppc_vbus_sink_enable);
	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	zassert_ok(board_set_active_charge_port(0));
	zassert_equal(ppc_vbus_sink_enable_fake.call_count, 2);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[0], 1);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[0], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg0_history[1], 0);
	zassert_equal(ppc_vbus_sink_enable_fake.arg1_history[1], 1);

	/* pd_power_supply_reset */
	pd_power_supply_reset(0);
	zassert_equal(ppc_vbus_source_enable_fake.call_count, 1);
	zassert_equal(ppc_vbus_source_enable_fake.arg0_val, 0);
	zassert_equal(ppc_vbus_source_enable_fake.arg1_val, 0);
	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE)) {
		zassert_equal(pd_set_vbus_discharge_fake.call_count, 1);
		zassert_equal(pd_set_vbus_discharge_fake.arg0_val, 0);
		zassert_equal(pd_set_vbus_discharge_fake.arg1_val, 1);
	}
	k_sleep(K_MSEC(10));

	/* pd_set_power_supply_ready */
	zassert_equal(pd_set_power_supply_ready(2), EC_ERROR_INVAL);

	RESET_FAKE(ppc_vbus_sink_enable);
	ppc_vbus_sink_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	ppc_vbus_sink_enable_fake.return_val = EC_SUCCESS;
	ppc_vbus_source_enable_fake.return_val = EC_ERROR_UNKNOWN;
	zassert_equal(pd_set_power_supply_ready(0), EC_ERROR_UNKNOWN);

	ppc_vbus_source_enable_fake.return_val = EC_SUCCESS;
	zassert_ok(pd_set_power_supply_ready(0));
	k_sleep(K_MSEC(10));

	/* typec_set_source_current_limit */
	typec_set_source_current_limit(0, TYPEC_RP_3A0);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 1);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg0_val, 0);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.arg1_val,
		      TYPEC_RP_3A0);

	ppc_set_vbus_source_current_limit_fake.return_val = EC_ERROR_UNKNOWN;
	typec_set_source_current_limit(1, TYPEC_RP_1A5);
	zassert_equal(ppc_set_vbus_source_current_limit_fake.call_count, 2);
}

static void test_before(void *fixture)
{
	RESET_FAKE(cros_cbi_get_fw_config);
	RESET_FAKE(cbi_get_ssfc);
	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(ppc_vbus_sink_enable);
	RESET_FAKE(ppc_is_vbus_present);
	RESET_FAKE(ppc_set_vbus_source_current_limit);
	RESET_FAKE(pd_set_vbus_discharge);

	clamshell_mode = false;
	ssfc_data = 0;
}

ZTEST_SUITE(teliks, NULL, NULL, test_before, NULL, NULL);
