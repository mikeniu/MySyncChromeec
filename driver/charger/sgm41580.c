/* sgm41580.c - Driver for SGM41580 SMBus NVDC Buck-Boost battery charger */
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "i2c.h"
#include "sgm41580.h"
#include "sgm41580_reg.h"
#include "system.h"
#include "timer.h"
#include "util.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "SGM41580 is a NVDC charger, enable CONFIG_CHARGER_NARROW_VDC."
#endif

/* TODO: FIXME - override with board-level config when available */
#define CONFIG_CHARGER_SGM41580_SENSE_RESISTOR    5  /* mΩ, RSR (charge) */
#define CONFIG_CHARGER_SGM41580_SENSE_RESISTOR_AC 5  /* mΩ, RAC (input) */

/*
 * DEFAULT_SENSE_RESISTOR_MOHM represents the sense resistor value that the
 * charger parameters (SGM_CHARGE_I_MAX/STEP, etc.) are expressed in.
 * The REG_TO_* / *_TO_REG macros convert between these "default" units
 * and the actual on-board sense resistor value (SENSE_RESISTOR_*_MOHM).
 * Set to 10mΩ to match sgm41538 convention used in sgm41580.h.
 */
#define DEFAULT_SENSE_RESISTOR_MOHM     10
#define SENSE_RESISTOR_CHARGE_MOHM      CONFIG_CHARGER_SGM41580_SENSE_RESISTOR
#define SENSE_RESISTOR_INPUT_MOHM       CONFIG_CHARGER_SGM41580_SENSE_RESISTOR_AC

#define REG_TO_CHARGE_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_CHARGE_MOHM)
#define CHARGE_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_CHARGE_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

#define REG_TO_INPUT_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_INPUT_MOHM)
#define INPUT_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_INPUT_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

#define REG_TO_OTG_CURRENT(reg) \
    (((reg) * DEFAULT_SENSE_RESISTOR_MOHM) / SENSE_RESISTOR_INPUT_MOHM)
#define OTG_CURRENT_TO_REG(cur) \
    (((cur) * SENSE_RESISTOR_INPUT_MOHM) / DEFAULT_SENSE_RESISTOR_MOHM)

/* Charger info */
static const struct charger_info sgm41580_charger_info = {
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

/* Convert charge voltage (mV) to register value (8mV steps) */
static int charge_voltage_to_reg(int mv)
{
    int dac = 0;

    if (mv < SGM_CHARGE_V_MIN)
        dac = SGM_CHARGE_V_MIN;
    else if (mv > SGM_CHARGE_V_MAX)
        dac = SGM_CHARGE_V_MAX;
    else
        dac = mv;

    return ((dac / SGM_CHARGE_V_STEP) << SGM41580_CHARGE_VOLTAGE_DAC_SHIFT);
}

/* Convert register value to charge voltage (mV) */
static int reg_to_charge_voltage(int reg)
{
    int mv = (reg >> SGM41580_CHARGE_VOLTAGE_DAC_SHIFT) * SGM_CHARGE_V_STEP;
    if (mv > SGM_CHARGE_V_MAX)
        mv = SGM_CHARGE_V_MAX;
    return mv;
}

/* Convert charge current (mA, 10mΩ units) to register value */
static int charge_current_to_reg(int ma)
{
    int dac = 0;

    if (ma < SGM_CHARGE_I_MIN)
        return 0;
    else if (ma > SGM_CHARGE_I_MAX)
        dac = SGM_CHARGE_I_MAX;
    else
        dac = ma;

    return (dac / SGM_CHARGE_I_STEP) << SGM41580_CHARGE_CURRENT_DAC_SHIFT;
}

/* Convert register value to charge current (mA, 10mΩ units) */
static int reg_to_charge_current(int reg)
{
    int ma = (reg >> SGM41580_CHARGE_CURRENT_DAC_SHIFT) * SGM_CHARGE_I_STEP;
    if (ma > SGM_CHARGE_I_MAX)
        ma = SGM_CHARGE_I_MAX;
    return ma;
}

/* Convert input current (mA, 10mΩ units) to register value */
static int input_current_to_reg(int ma)
{
    if (ma <= 0)
        return 0;
    if (ma < SGM_INPUT_I_MIN)
        ma = SGM_INPUT_I_MIN;
    else if (ma > SGM_INPUT_I_MAX)
        ma = SGM_INPUT_I_MAX;
    return (ma / SGM_INPUT_I_STEP) << SGM41580_IIN_HOST_DAC_SHIFT;
}

/* Convert register value to input current (mA, 10mΩ units) */
static int reg_to_input_current(int reg)
{
    int ma = (reg >> SGM41580_IIN_HOST_DAC_SHIFT) * SGM_INPUT_I_STEP;
    if (ma > SGM_INPUT_I_MAX)
        ma = SGM_INPUT_I_MAX;
    return ma;
}

/* Convert OTG voltage (mV) to register value (8mV steps) */
static int otg_voltage_to_reg(int mv)
{
    int dac = 0;

    if (mv < SGM_OTG_V_MIN)
        dac = SGM_OTG_V_MIN;
    else if (mv > SGM_OTG_V_MAX)
        dac = SGM_OTG_V_MAX;
    else
        dac = mv;

    return ((dac / SGM_OTG_V_STEP) << SGM41580_OTG_VOLTAGE_DAC_SHIFT);
}

/* Convert OTG current (mA, 10mΩ units) to register value */
static int otg_current_to_reg(int ma)
{
    if (ma <= 0)
        return 0;
    if (ma < SGM_OTG_I_MIN)
        ma = SGM_OTG_I_MIN;
    else if (ma > SGM_OTG_I_MAX)
        ma = SGM_OTG_I_MAX;
    return (ma / SGM_OTG_I_STEP) << SGM41580_OTG_CURRENT_DAC_SHIFT;
}

/* Convert VSYS_MIN (mV) to register value (100mV steps) */
static int vsys_min_to_reg(int mv)
{
    int dac = 0;

    if (mv < SGM_VSYSMIN_V_MIN)
        dac = SGM_VSYSMIN_V_MIN;
    else if (mv > SGM_VSYSMIN_V_MAX)
        dac = SGM_VSYSMIN_V_MAX;
    else
        dac = mv;

    return (dac / SGM_VSYSMIN_V_STEP) << SGM41580_VSYS_MIN_DAC_SHIFT;
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
static int sgm41580_set_low_power_mode(int chgnum, int enable)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_0, EN_LWPWR, enable, reg);
    return raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_0, reg);
}

#if 0
static int sgm41580_get_low_power_mode(int chgnum, int *mode)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    *mode = GET_SGM_FIELD(SGM41580_CHARGE_OPTION_0, EN_LWPWR, reg);
    return EC_SUCCESS;
}
#endif

/* Initialize sense resistor configuration */
static void init_sense_resistors(int chgnum)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_1, &reg);
    if (rv)
        return;
    /* RSNS_RAC: 0 = 10mΩ, 1 = 5mΩ */
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_1, RSNS_RAC,
                        (SENSE_RESISTOR_INPUT_MOHM == 5) ? 1 : 0, reg);
    /* RSNS_RSR: 0 = 10mΩ, 1 = 5mΩ */
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_1, RSNS_RSR,
                        (SENSE_RESISTOR_CHARGE_MOHM == 5) ? 1 : 0, reg);
    /*
     * EN_FAST_5MOHM: only meaningful with 5mΩ RAC.
     * 1 = clamp IIN_HOST at 6.4A (default), 0 = allow up to 10A.
     */
    if (SENSE_RESISTOR_INPUT_MOHM == 5)
        reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_1, EN_FAST_5MOHM, 1, reg);
    raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_1, reg);
}

/* Initialize PROCHOT options */
static void init_prochot(int chgnum)
{
    int reg;
    /* Configure PROCHOT_OPTION_0 */
    if (raw_read16(chgnum, SGM41580_REG_PROCHOT_OPTION_0, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41580_PROCHOT_OPTION_0, ILIM2_VTH, 9, reg);   /* 150% */
        reg = SET_SGM_FIELD(SGM41580_PROCHOT_OPTION_0, ICRIT_DEG, 1, reg);   /* 100us */
        raw_write16(chgnum, SGM41580_REG_PROCHOT_OPTION_0, reg);
    }
    /* Enable necessary PROCHOT events (ICRIT + IDCHG) */
    if (raw_read16(chgnum, SGM41580_REG_PROCHOT_OPTION_1, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41580_PROCHOT_OPTION_1, PROFILE_ICRIT, 1, reg);
        reg = SET_SGM_FIELD(SGM41580_PROCHOT_OPTION_1, PROFILE_IDCHG, 1, reg);
        raw_write16(chgnum, SGM41580_REG_PROCHOT_OPTION_1, reg);
    }
}

/* Initialize charger */
static void sgm41580_init(int chgnum)
{
    int reg;
    /* Exit low power mode for configuration */
    sgm41580_set_low_power_mode(chgnum, 0);
    /* Reset to default if needed (not jumped) */
    if (!system_jumped_late()) {
        if (raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_3, &reg) == 0) {
            /* RESET_REG (D14) could be used here to reset all registers. */
        }
    }
    init_sense_resistors(chgnum);
    init_prochot(chgnum);
    /* Enable IBAT buffer for current monitoring */
    if (raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_1, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_1, EN_IBAT, 1, reg);
        raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_1, reg);
    }
    /* Enable external ILIM_HIZ pin usage */
    if (raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_2, &reg) == 0) {
        reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_2, EN_EXTILIM, 1, reg);
        raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_2, reg);
    }
    /* Re-enable low power mode */
    sgm41580_set_low_power_mode(chgnum, 1);
}

static enum ec_error_list sgm41580_post_init(int chgnum)
{
    return EC_SUCCESS;
}

static const struct charger_info *sgm41580_get_info(int chgnum)
{
    return &sgm41580_charger_info;
}

static enum ec_error_list sgm41580_get_status(int chgnum, int *status)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    *status = CHARGER_LEVEL_2;
    if (GET_SGM_FIELD(SGM41580_CHARGE_OPTION_0, CHRG_INHIBIT, reg))
        *status |= CHARGER_CHARGE_INHIBITED;
    return EC_SUCCESS;
}

static enum ec_error_list sgm41580_set_mode(int chgnum, int mode)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_0, CHRG_INHIBIT,
                        !!(mode & CHARGER_CHARGE_INHIBITED), reg);
    return raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_0, reg);
}

static enum ec_error_list sgm41580_enable_otg_power(int chgnum, int enabled)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_3, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_3, EN_OTG, !!enabled, reg);
    return raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_3, reg);
}

static enum ec_error_list sgm41580_set_otg_current_voltage(int chgnum,
                                                           int out_cur,
                                                           int out_vol)
{
    int rv;
    int cur_reg = otg_current_to_reg(OTG_CURRENT_TO_REG(out_cur));
    int vol_reg = otg_voltage_to_reg(out_vol);

    rv = raw_write16(chgnum, SGM41580_REG_OTG_CURRENT, cur_reg);
    if (rv)
        return rv;
    return raw_write16(chgnum, SGM41580_REG_OTG_VOLTAGE, vol_reg);
}

static enum ec_error_list sgm41580_get_current(int chgnum, int *current)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_CURRENT, &reg);
    if (rv)
        return rv;
    *current = REG_TO_CHARGE_CURRENT(reg_to_charge_current(reg));
    return EC_SUCCESS;
}

static enum ec_error_list sgm41580_set_current(int chgnum, int current)
{
    int reg = charge_current_to_reg(CHARGE_CURRENT_TO_REG(current));
    return raw_write16(chgnum, SGM41580_REG_CHARGE_CURRENT, reg);
}

static enum ec_error_list sgm41580_get_voltage(int chgnum, int *voltage)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_VOLTAGE, &reg);
    if (rv)
        return rv;
    *voltage = reg_to_charge_voltage(reg);
    return EC_SUCCESS;
}

static enum ec_error_list sgm41580_set_voltage(int chgnum, int voltage)
{
    int reg = charge_voltage_to_reg(voltage);
    return raw_write16(chgnum, SGM41580_REG_CHARGE_VOLTAGE, reg);
}

static enum ec_error_list sgm41580_discharge_on_ac(int chgnum, int enable)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, &reg);
    if (rv)
        return rv;
    reg = SET_SGM_FIELD(SGM41580_CHARGE_OPTION_0, EN_LEARN, enable, reg);
    return raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_0, reg);
}

static enum ec_error_list sgm41580_set_input_current_limit(int chgnum, int ma)
{
    int reg = input_current_to_reg(INPUT_CURRENT_TO_REG(ma));
    return raw_write16(chgnum, SGM41580_REG_IIN_HOST, reg);
}

static enum ec_error_list sgm41580_get_input_current_limit(int chgnum, int *ma)
{
    int reg, rv;
    rv = raw_read16(chgnum, SGM41580_REG_IIN_HOST, &reg);
    if (rv)
        return rv;
    /* IIN_HOST holds the host-programmed limit; IIN_DPM (0x22) reflects the
     * actual limit after ICO. Use IIN_HOST here for the host-set value. */
    *ma = REG_TO_INPUT_CURRENT(reg_to_input_current(reg));
    return EC_SUCCESS;
}

static enum ec_error_list sgm41580_get_input_current(int chgnum, int *ma)
{
    /* No direct ADC read here; return the current limit setting. */
    return sgm41580_get_input_current_limit(chgnum, ma);
}

static enum ec_error_list sgm41580_manufacturer_id(int chgnum, int *id)
{
    return raw_read16(chgnum, SGM41580_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list sgm41580_device_id(int chgnum, int *id)
{
    return raw_read16(chgnum, SGM41580_REG_DEVICE_ID, id);
}

static enum ec_error_list sgm41580_get_option(int chgnum, int *option)
{
    return raw_read16(chgnum, SGM41580_REG_CHARGE_OPTION_0, option);
}

static enum ec_error_list sgm41580_set_option(int chgnum, int option)
{
    return raw_write16(chgnum, SGM41580_REG_CHARGE_OPTION_0, option);
}

enum ec_error_list sgm41580_get_option_reg(int chgnum, int reg, int *option)
{
    return raw_read16(chgnum, reg, option);
}

enum ec_error_list sgm41580_set_option_reg(int chgnum, int reg, int option)
{
    return raw_write16(chgnum, reg, option);
}

int sgm41580_set_min_system_voltage(int chgnum, int mv)
{
    int reg = vsys_min_to_reg(mv);
    return raw_write16(chgnum, SGM41580_REG_VSYS_MIN, reg);
}

#include "hooks.h"
/* TODO: FIXME */
/* reference bq25710 */
/* Called on AP S5 -> S3 and S3/S0iX -> S0 transition */
static void sgm41580_chipset_startup(void)
{
    sgm41580_set_low_power_mode(CHARGER_SOLO, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, sgm41580_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, sgm41580_chipset_startup, HOOK_PRIO_DEFAULT);
/* Called on AP S0 -> S0iX/S3 or S3 -> S5 transition */
static void sgm41580_chipset_suspend(void)
{
    sgm41580_set_low_power_mode(CHARGER_SOLO, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, sgm41580_chipset_suspend, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, sgm41580_chipset_suspend, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_CHARGER_DUMP
static void console_sgm41580_dump_regs(int chgnum)
{
    int i, val;
    static const uint8_t regs[] = {
        SGM41580_REG_CHARGE_OPTION_0,
        SGM41580_REG_CHARGE_CURRENT,
        SGM41580_REG_CHARGE_VOLTAGE,
        SGM41580_REG_CHARGER_STATUS,
        SGM41580_REG_PROCHOT_STATUS,
        SGM41580_REG_IIN_DPM,
        SGM41580_REG_CHARGE_OPTION_1,
        SGM41580_REG_CHARGE_OPTION_2,
        SGM41580_REG_CHARGE_OPTION_3,
        SGM41580_REG_PROCHOT_OPTION_0,
        SGM41580_REG_PROCHOT_OPTION_1,
        SGM41580_REG_CHARGE_OPTION_4,
        SGM41580_REG_OTG_VOLTAGE,
        SGM41580_REG_OTG_CURRENT,
        SGM41580_REG_VSYS_MIN,
        SGM41580_REG_IIN_HOST,
        SGM41580_REG_MANUFACTURER_ID,
        SGM41580_REG_DEVICE_ID,
    };
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        if (raw_read16(chgnum, regs[i], &val))
            continue;
        ccprintf("SGM41580 REG 0x%02x: 0x%04x\n", regs[i], val);
    }
}
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv sgm41580_drv = {
    .init                    = sgm41580_init,
    .post_init               = sgm41580_post_init,
    .get_info                = sgm41580_get_info,
    .get_status              = sgm41580_get_status,
    .set_mode                = sgm41580_set_mode,
    .enable_otg_power        = sgm41580_enable_otg_power,
    .set_otg_current_voltage = sgm41580_set_otg_current_voltage,
    .get_current             = sgm41580_get_current,
    .set_current             = sgm41580_set_current,
    .get_voltage             = sgm41580_get_voltage,
    .set_voltage             = sgm41580_set_voltage,
    .discharge_on_ac         = sgm41580_discharge_on_ac,
    .set_input_current_limit = sgm41580_set_input_current_limit,
    .get_input_current_limit = sgm41580_get_input_current_limit,
    .get_input_current       = sgm41580_get_input_current,
    .manufacturer_id         = sgm41580_manufacturer_id,
    .device_id               = sgm41580_device_id,
    .get_option              = sgm41580_get_option,
    .set_option              = sgm41580_set_option,
#ifdef CONFIG_CMD_CHARGER_DUMP
    .dump_registers          = console_sgm41580_dump_regs,
#endif
};
