/* sgm41538.h - Driver for SGM41538/SGM41538B battery charger */
#ifndef __CROS_EC_SGM41538_H
#define __CROS_EC_SGM41538_H

/* SMBus address */
#define SGM41538_SMBUS_ADDR               0x09
#define SGM41538B_SMBUS_ADDR              0x49

/* Charger parameters (with 10mΩ sense resistors) */
#define SGM_CHARGER_NAME                  "sgm41538"
#define SGM_CHARGE_V_MAX                  19200   /* mV */
#define SGM_CHARGE_V_MIN                  1024    /* mV */
#define SGM_CHARGE_V_STEP                 16      /* mV */
#define SGM_CHARGE_I_MAX                  8128    /* mA */
#define SGM_CHARGE_I_MIN                  128      /* mA */
#define SGM_CHARGE_I_STEP                 64      /* mA */
#define SGM_INPUT_I_MAX                   8128    /* mA */
#define SGM_INPUT_I_MIN                   64      /* mA */
#define SGM_INPUT_I_STEP                  64      /* mA */
#define SGM_DISCHARGE_I_MAX               32256   /* mA */
#define SGM_DISCHARGE_I_MIN               512     /* mA */
#define SGM_DISCHARGE_I_STEP              512     /* mA */
#define SGM_VSYSMIN_V_MAX                 13568   /* mV */
#define SGM_VSYSMIN_V_MIN                 5632     /* mV */
#define SGM_VSYSMIN_V_STEP                128     /* mV */

/* Register addresses */
#define SGM41538_REG_CHARGE_OPTION_0      0x12
#define SGM41538_REG_CHARGE_OPTION_1      0x3B
#define SGM41538_REG_CHARGE_OPTION_2      0x38
#define SGM41538_REG_CHARGE_OPTION_3      0x37
#define SGM41538_REG_PROCHOT_OPTION_0     0x3C
#define SGM41538_REG_PROCHOT_OPTION_1     0x3D
#define SGM41538_REG_PROCHOT_STATUS       0x3A
#define SGM41538_REG_CHARGE_CURRENT       0x14
#define SGM41538_REG_CHARGE_VOLTAGE       0x15
#define SGM41538_REG_DISCHARGE_CURRENT    0x39
#define SGM41538_REG_INPUT_CURRENT        0x3F
#define SGM41538_REG_VSYS_MIN             0x3E
#define SGM41538_REG_MANUFACTURER_ID      0xFE
#define SGM41538_REG_DEVICE_ID            0xFF

extern const struct charger_drv sgm41538_drv;

/* Public functions */
enum ec_error_list sgm41538_get_option_reg(int chgnum, int reg, int *option);
enum ec_error_list sgm41538_set_option_reg(int chgnum, int reg, int option);
int sgm41538_set_min_system_voltage(int chgnum, int mv);

#endif /* __CROS_EC_SGM41538_H */