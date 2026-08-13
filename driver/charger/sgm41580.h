/* sgm41580.h - Driver for SGM41580 SMBus NVDC Buck-Boost battery charger */
#ifndef __CROS_EC_SGM41580_H
#define __CROS_EC_SGM41580_H

/* SMBus address */
#define SGM41580_SMBUS_ADDR              0x09

/* Charger parameters (with 10mΩ sense resistors)
 * Register DAC in the datasheet is referenced to a 5mΩ sense resistor.
 * With a 10mΩ sense resistor the per-bit current is halved.
 * The values below are the actual measurable limits at 10mΩ.
 */
#define SGM_CHARGER_NAME                 "sgm41580"
#define SGM_CHARGE_V_MAX                 27600   /* mV, 8mV resolution */
#define SGM_CHARGE_V_MIN                 1024    /* mV */
#define SGM_CHARGE_V_STEP                8       /* mV */
#define SGM_CHARGE_I_MAX                 8128    /* mA @ 10mΩ, 64mA step (127 steps) */
#define SGM_CHARGE_I_MIN                 64      /* mA, 1 LSB */
#define SGM_CHARGE_I_STEP                64      /* mA */
#define SGM_INPUT_I_MAX                  6350    /* mA @ 10mΩ, 50mA step (127 steps) */
#define SGM_INPUT_I_MIN                  50      /* mA, 1 LSB */
#define SGM_INPUT_I_STEP                 50      /* mA */
#define SGM_OTG_V_MAX                    28160   /* mV, 8mV resolution */
#define SGM_OTG_V_MIN                    3000    /* mV */
#define SGM_OTG_V_STEP                   8       /* mV */
#define SGM_OTG_I_MAX                    6350    /* mA @ 10mΩ, 50mA step (127 steps) */
#define SGM_OTG_I_MIN                    50      /* mA, 1 LSB */
#define SGM_OTG_I_STEP                   50      /* mA */
#define SGM_VSYSMIN_V_MAX                25500   /* mV, 100mV resolution */
#define SGM_VSYSMIN_V_MIN                1000    /* mV */
#define SGM_VSYSMIN_V_STEP               100     /* mV */

#define SGM41580_MANUFACTURER_ID_VAL     0x07
#define SGM41580_DEVICE_ID_VAL           0x80

/* Register addresses */
#define SGM41580_REG_CHARGE_OPTION_0     0x12
#define SGM41580_REG_CHARGE_CURRENT      0x14
#define SGM41580_REG_CHARGE_VOLTAGE      0x15
#define SGM41580_REG_CHARGER_STATUS      0x20
#define SGM41580_REG_PROCHOT_STATUS      0x21
#define SGM41580_REG_IIN_DPM             0x22
#define SGM41580_REG_ADC_VBUS_PSYS       0x23
#define SGM41580_REG_VBUS_A12_IBAT       0x24
#define SGM41580_REG_ADC_IIN_CMPIN       0x25
#define SGM41580_REG_ADC_VSYS_VBAT       0x26
#define SGM41580_REG_CHARGE_OPTION_1     0x30
#define SGM41580_REG_CHARGE_OPTION_2     0x31
#define SGM41580_REG_CHARGE_OPTION_3     0x32
#define SGM41580_REG_PROCHOT_OPTION_0    0x33
#define SGM41580_REG_PROCHOT_OPTION_1    0x34
#define SGM41580_REG_ADC_OPTION          0x35
#define SGM41580_REG_CHARGE_OPTION_4     0x36
#define SGM41580_REG_VMIN_ACTIVE_PROT    0x37
#define SGM41580_REG_OTG_VOLTAGE         0x3B
#define SGM41580_REG_OTG_CURRENT         0x3C
#define SGM41580_REG_INPUT_VOLTAGE       0x3D
#define SGM41580_REG_VSYS_MIN            0x3E
#define SGM41580_REG_IIN_HOST            0x3F
#define SGM41580_REG_MANUFACTURER_ID     0xFE
#define SGM41580_REG_DEVICE_ID           0xFF

extern const struct charger_drv sgm41580_drv;

/* Public functions */
enum ec_error_list sgm41580_get_option_reg(int chgnum, int reg, int *option);
enum ec_error_list sgm41580_set_option_reg(int chgnum, int reg, int option);
int sgm41580_set_min_system_voltage(int chgnum, int mv);

#endif /* __CROS_EC_SGM41580_H */
