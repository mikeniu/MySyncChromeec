/* sgm41538.c - Driver for SGM41538/SGM41538B battery charger */
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "i2c.h"
#include "sgm41538.h"
#include "sgm41538_reg.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "SGM41538 is a NVDC charger, enable CONFIG_CHARGER_NARROW_VDC."
#endif

/* TODO: FIXME */
#define CONFIG_CHARGER_SGM41538_SENSE_RESISTOR 10
#define CONFIG_CHARGER_SGM41538_SENSE_RESISTOR_AC 10

/* Sense resistor configuration (default 10mΩ) */
#define DEFAULT_SENSE_RESISTOR_MOHM     10
#define SENSE_RESISTOR_CHARGE_MOHM      CONFIG_CHARGER_SGM41538_SENSE_RESISTOR
#define SENSE_RESISTOR_INPUT_MOHM       CONFIG_CHARGER_SGM41538_SENSE_RESISTOR_AC

#define REG_TO_CHARGE_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_CHARGE_MOHM)
#define CHARGE_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_CHARGE_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

#define REG_TO_INPUT_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_INPUT_MOHM)
#define INPUT_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_INPUT_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

#define REG_TO_DISCHARGE_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_CHARGE_MOHM)
#define DISCHARGE_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_CHARGE_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

/* Charger info */
static const struct charger_info sgm41538_charger_info = {
    .name               = SGM_CHARGER_NAME,
    .voltage_max        = SGM_CHARGE_V_MAX,
    .voltage_min        = SGM_CHARGE_V_MIN,
    .voltage_step       = SGM_CHARGE_V_STEP,
    .current_max        = REG_TO_CHARGE_CURRENT(SGM_CHARGE_I_MAX),
    .current_min        = REG_TO_CHARGE_CURRENT(SGM_CHARGE_I_MIN),
    .current_step       = REG_TO_CHARGE_CURRENT(SGM_CHARGE_I_STEP),
    .input_current_max  = REG_TO_INPUT_CURRENT(SGM_INPUT_I_MAX),
    .input_current_min  = REG_TO_INPUT_CURRENT(SGM_INPUT_I_MIN),
    .input_current_step = REG_TO_INPUT_CURRENT(SGM_INPUT_I_STEP),
};

/* Convert charge voltage (mV) to register value (16mV steps) */
static int charge_voltage_to_reg(int mv)
{
	int dac = 0;

	if (mv < SGM_CHARGE_V_MIN)
		dac = SGM_CHARGE_V_MIN;
	else if (mv > SGM_CHARGE_V_MAX)
		dac = SGM_CHARGE_V_MAX;
	else
		dac = mv;

    return ((dac / SGM_CHARGE_V_STEP) << SGM41538_CHARGE_VOLTAGE_DAC_SHIFT);
}

/* Convert register value to charge voltage (mV) */
static int reg_to_charge_voltage(int reg)
{
    int mv = (reg  >> SGM41538_CHARGE_VOLTAGE_DAC_SHIFT) * SGM_CHARGE_V_STEP;
	if (mv > SGM_CHARGE_V_MAX)
		mv = SGM_CHARGE_V_MAX;
    return mv;
}

/* Convert charge current (mA) to register value (64mA steps) */
static int charge_current_to_reg(int ma)
{
    int dac = 0;

	if (ma < SGM_CHARGE_I_MIN)
		return 0;
	else if (ma > SGM_CHARGE_I_MAX)
		dac = SGM_CHARGE_I_MAX;
	else
		dac = ma;

    return (dac / SGM_CHARGE_I_STEP) << SGM41538_CHARGE_CURRENT_DAC_SHIFT;
}

/* Convert register value to charge current (mA) */
static int reg_to_charge_current(int reg)
{
    int ma = (reg  >> SGM41538_CHARGE_CURRENT_DAC_SHIFT) * SGM_CHARGE_I_STEP;
	if (ma > SGM_CHARGE_I_MAX)
		ma = SGM_CHARGE_I_MAX;
    return ma;
}

/* Convert input current (mA) to register value (64mA steps) */
static int input_current_to_reg(int ma)
{
    if (ma <= 0)
        return 0;
    if (ma < SGM_INPUT_I_MIN)
        ma = SGM_INPUT_I_MIN;
    else if (ma > SGM_INPUT_I_MAX)
        ma = SGM_INPUT_I_MAX;
    return (ma / SGM_INPUT_I_STEP) << SGM41538_INPUT_CURRENT_DAC_SHIFT;
}

/* Convert register value to input current (mA) */
static int reg_to_input_current(int reg)
{
    int ma = (reg  >> SGM41538_INPUT_CURRENT_DAC_SHIFT) * SGM_INPUT_I_STEP;
	if (ma > SGM_INPUT_I_MAX)
		ma = SGM_INPUT_I_MAX;
    return ma;
}

#if 0
/* Convert discharge current (mA) to register value (512mA steps) */
static int discharge_current_to_reg(int ma)
{
    if (ma <= 0)
        return 0;
    if (ma < SGM_DISCHARGE_I_MIN)
        ma = SGM_DISCHARGE_I_MIN;
    else if (ma > SGM_DISCHARGE_I_MAX)
        ma = SGM_DISCHARGE_I_MAX;
    return (ma / SGM_DISCHARGE_I_STEP) << SGM41538_DISCHARGE_CURRENT_DAC_SHIFT;
}

/* Convert register value to discharge current (mA) */
static int reg_to_discharge_current(int reg)
{
    int ma = (reg  >> SGM41538_DISCHARGE_CURRENT_DAC_SHIFT) * SGM_DISCHARGE_I_STEP;
	if (ma > SGM_DISCHARGE_I_MAX)
		ma = SGM_DISCHARGE_I_MAX;
    return ma;
}
#endif

/* Convert VSYS_MIN (mV) to register value (128mV steps) */
static int vsys_min_to_reg(int mv)
{
    int dac = 0;

	if (mv < SGM_VSYSMIN_V_MIN)
		dac = SGM_VSYSMIN_V_MIN;
	else if (mv > SGM_VSYSMIN_V_MAX)
		dac = SGM_VSYSMIN_V_MAX;
	else
		dac = mv;

    return (dac / SGM_VSYSMIN_V_STEP) << SGM41538_VSYS_MIN_DAC_SHIFT;
}

/* I2C helpers */
static inline int raw_read16(int chgnum, int reg, int *value)
{
    return i2c_read16(chg_chips[chgnum].i2c_port,
                      chg_chips[chgnum].i2c_addr_flags,
                      reg, value);
}

static inline int raw_write16(int chgnum, int reg, int value)
{
    return i2c_write16(chg_chips[chgnum].i2c_port,
                       chg_chips[chgnum].i2c_addr_flags,
                       reg, value);
}

/* Low power mode control */
static int sgm41538_set_low_power_mode(int chgnum, int enable)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_0, EN_LWPWR, enable, reg);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_0, reg);
}

#if 0
static int sgm41538_get_low_power_mode(int chgnum, int *mode)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    *mode = GET_SGM_FIELD(SGM41538_CHARGE_OPTION_0, EN_LWPWR, reg);
    return EC_SUCCESS;
}
#endif

/* Initialize sense resistor configuration */
static void init_sense_resistors(int chgnum)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_1, &reg);
    if (rv)
        return;
    /* Set RSNS_RATIO based on sense resistors */
    if (SENSE_RESISTOR_INPUT_MOHM == SENSE_RESISTOR_CHARGE_MOHM)
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, RSNS_RATIO, 0, reg);
    else if (SENSE_RESISTOR_INPUT_MOHM == 2 * SENSE_RESISTOR_CHARGE_MOHM)
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, RSNS_RATIO, 1, reg);
    else if (SENSE_RESISTOR_INPUT_MOHM * 2 == SENSE_RESISTOR_CHARGE_MOHM)
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, RSNS_RATIO, 2, reg);
    else
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, RSNS_RATIO, 0, reg);
    raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_1, reg);
}

/* Initialize PROCHOT options */
static void init_prochot(int chgnum)
{
    int reg;
    /* Configure PROCHOT_OPTION_0 */
    if (raw_read16(chgnum, SGM41538_REG_PROCHOT_OPTION_0, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41538_PROCHOT_OPTION_0, ILIM2_VTH, 9, reg); /* 150% */
        reg = SET_SGM_FIELD(SGM41538_PROCHOT_OPTION_0, ICRIT_DEG, 1, reg); /* 100us */
        reg = SET_SGM_FIELD(SGM41538_PROCHOT_OPTION_0, VSYS_VTH, 1, reg); /* 6.0V */
        reg = SET_SGM_FIELD(SGM41538_PROCHOT_OPTION_0, PROCHOT_WIDTH, 2, reg); /* 10ms */
        raw_write16(chgnum, SGM41538_REG_PROCHOT_OPTION_0, reg);
    }
    /* Enable necessary PROCHOT events (ICRIT, IDCHG) */
    if (raw_read16(chgnum, SGM41538_REG_PROCHOT_OPTION_1, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41538_PROCHOT_OPTION_1, PROCHOT_PROFILE,
                            (1 << 5) | (1 << 3), reg); /* ICRIT + IDCHG */
        raw_write16(chgnum, SGM41538_REG_PROCHOT_OPTION_1, reg);
    }
}

/* Initialize charger */
static void sgm41538_init(int chgnum)
{
    int reg;
    /* Exit low power mode for configuration */
    sgm41538_set_low_power_mode(chgnum, 0);
    /* Reset to default if needed (not jumped) */
    if (!system_jumped_late()) {
        if (raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_3, &reg) == 0) {
            /* Write 1 to bit14? Not defined, skip reset. */
        }
    }
    init_sense_resistors(chgnum);
    init_prochot(chgnum);
    /* Enable IADP and IDCHG buffers, PMON */
    if (raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_1, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, EN_IDCHG, 1, reg);
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_1, EN_PMON, 1, reg);
        raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_1, reg);
    }
    /* Enable external ILIM pin usage */
    if (raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_2, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_2, EN_EXTILIM, 1, reg);
        raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_2, reg);
    }
    /* Re-enable low power mode */
    sgm41538_set_low_power_mode(chgnum, 1);
}

static enum ec_error_list sgm41538_post_init(int chgnum)
{
    return EC_SUCCESS;
}

static const struct charger_info *sgm41538_get_info(int chgnum)
{
    return &sgm41538_charger_info;
}

static enum ec_error_list sgm41538_get_status(int chgnum, int *status)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    *status = CHARGER_LEVEL_2;
    if (GET_SGM_FIELD(SGM41538_CHARGE_OPTION_0, CHRG_INHIBIT, reg))
        *status |= CHARGER_CHARGE_INHIBITED;
    return EC_SUCCESS;
}

static enum ec_error_list sgm41538_set_mode(int chgnum, int mode)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_0, CHRG_INHIBIT,
                        !! (mode& CHARGER_CHARGE_INHIBITED), reg);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_0, reg);
}

static enum ec_error_list sgm41538_enable_otg_power(int chgnum, int enabled)
{
    /* OTG not supported in basic mode */

	int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_2, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_2, EN_BATT_BOOST,
                        !!enabled, reg);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_2, reg);

}

static enum ec_error_list sgm41538_set_otg_current_voltage(int chgnum,
                                                           int out_cur,
                                                           int out_vol)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_2, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_2, VBOOST,
                        !!out_vol, reg);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_2, reg);
}

static enum ec_error_list sgm41538_get_current(int chgnum, int *current)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_CURRENT, &reg);
    if (rv)
        return rv;
    *current = REG_TO_CHARGE_CURRENT(reg_to_charge_current(reg));
    return EC_SUCCESS;
}

static enum ec_error_list sgm41538_set_current(int chgnum, int current)
{
    int reg = charge_current_to_reg(CHARGE_CURRENT_TO_REG(current));
    return raw_write16(chgnum, SGM41538_REG_CHARGE_CURRENT, reg);
}

static enum ec_error_list sgm41538_get_voltage(int chgnum, int *voltage)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_VOLTAGE, &reg);
    if (rv)
        return rv;
    *voltage = reg_to_charge_voltage(reg);
    return EC_SUCCESS;
}

static enum ec_error_list sgm41538_set_voltage(int chgnum, int voltage)
{
    int reg = charge_voltage_to_reg(voltage);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_VOLTAGE, reg);
}

static enum ec_error_list sgm41538_discharge_on_ac(int chgnum, int enable)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41538_CHARGE_OPTION_0, EN_LEARN, enable, reg);
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_0, reg);
}

static enum ec_error_list sgm41538_set_input_current_limit(int chgnum, int ma)
{
    int reg = input_current_to_reg(INPUT_CURRENT_TO_REG(ma));
    return raw_write16(chgnum, SGM41538_REG_INPUT_CURRENT, reg);
}

static enum ec_error_list sgm41538_get_input_current_limit(int chgnum, int *ma)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41538_REG_INPUT_CURRENT, &reg);
    if (rv)
        return rv;
    /* IIN_DPM is not available, use IIN_HOST value */
    *ma = REG_TO_INPUT_CURRENT(reg_to_input_current(reg));
    return EC_SUCCESS;
}

static enum ec_error_list sgm41538_get_input_current(int chgnum, int *ma)
{
    /* No direct ADC for input current; return current limit */
    return sgm41538_get_input_current_limit(chgnum, ma);
}

static enum ec_error_list sgm41538_manufacturer_id(int chgnum, int *id)
{
    return raw_read16(chgnum, SGM41538_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list sgm41538_device_id(int chgnum, int *id)
{
    return raw_read16(chgnum, SGM41538_REG_DEVICE_ID, id);
}

static enum ec_error_list sgm41538_get_option(int chgnum, int *option)
{
    return raw_read16(chgnum, SGM41538_REG_CHARGE_OPTION_0, option);
}

static enum ec_error_list sgm41538_set_option(int chgnum, int option)
{
    return raw_write16(chgnum, SGM41538_REG_CHARGE_OPTION_0, option);
}

enum ec_error_list sgm41538_get_option_reg(int chgnum, int reg, int *option)
{
    return raw_read16(chgnum, reg, option);
}

enum ec_error_list sgm41538_set_option_reg(int chgnum, int reg, int option)
{
    return raw_write16(chgnum, reg, option);
}

int sgm41538_set_min_system_voltage(int chgnum, int mv)
{
    int reg = vsys_min_to_reg(mv);
    return raw_write16(chgnum, SGM41538_REG_VSYS_MIN, reg);
}

#ifdef CONFIG_CMD_CHARGER_DUMP
static void console_sgm41538_dump_regs(int chgnum)
{
    int i, val;
    static const uint8_t regs[] = {
        SGM41538_REG_CHARGE_OPTION_0,
        SGM41538_REG_CHARGE_OPTION_1,
        SGM41538_REG_CHARGE_OPTION_2,
        SGM41538_REG_CHARGE_OPTION_3,
        SGM41538_REG_PROCHOT_OPTION_0,
        SGM41538_REG_PROCHOT_OPTION_1,
        SGM41538_REG_PROCHOT_STATUS,
        SGM41538_REG_CHARGE_CURRENT,
        SGM41538_REG_CHARGE_VOLTAGE,
        SGM41538_REG_DISCHARGE_CURRENT,
        SGM41538_REG_INPUT_CURRENT,
        SGM41538_REG_VSYS_MIN,
        SGM41538_REG_MANUFACTURER_ID,
        SGM41538_REG_DEVICE_ID,
    };
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        if (raw_read16(chgnum, regs[i], &val))
            continue;
        ccprintf("SGM41538 REG 0x%02x: 0x%04x\n", regs[i], val);
    }
}
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv sgm41538_drv = {
    .init                   = sgm41538_init,
    .post_init              = sgm41538_post_init,
    .get_info               = sgm41538_get_info,
    .get_status             = sgm41538_get_status,
    .set_mode               = sgm41538_set_mode,
    .enable_otg_power       = sgm41538_enable_otg_power,
    .set_otg_current_voltage = sgm41538_set_otg_current_voltage,
    .get_current            = sgm41538_get_current,
    .set_current            = sgm41538_set_current,
    .get_voltage            = sgm41538_get_voltage,
    .set_voltage            = sgm41538_set_voltage,
    .discharge_on_ac        = sgm41538_discharge_on_ac,
    .set_input_current_limit = sgm41538_set_input_current_limit,
    .get_input_current_limit = sgm41538_get_input_current_limit,
    .get_input_current      = sgm41538_get_input_current,
    .manufacturer_id        = sgm41538_manufacturer_id,
    .device_id              = sgm41538_device_id,
    .get_option             = sgm41538_get_option,
    .set_option             = sgm41538_set_option,
#ifdef CONFIG_CMD_CHARGER_DUMP
    .dump_registers         = console_sgm41538_dump_regs,
#endif
};