# -*- makefile -*-
# Copyright 2014 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Drivers for off-chip devices
#

# Note that this variable includes the trailing "/"
_driver_cur_dir:=$(dir $(lastword $(MAKEFILE_LIST)))

# Accelerometers
driver-$(CONFIG_ACCEL_BMA255)+=accel_bma2x2.o
driver-$(CONFIG_ACCEL_KXCJ9)+=accel_kionix.o
driver-$(CONFIG_ACCEL_KX022)+=accel_kionix.o
driver-$(CONFIG_ACCELGYRO_LSM6DS0)+=accelgyro_lsm6ds0.o
driver-$(CONFIG_ACCELGYRO_BMI160)+=accelgyro_bmi160.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI220)+=accelgyro_bmi260.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI260)+=accelgyro_bmi260.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCELGYRO_BMI3XX)+=accelgyro_bmi3xx.o accelgyro_bmi_common.o
driver-$(CONFIG_ACCEL_BMA4XX)+=accel_bma4xx.o
driver-$(CONFIG_MAG_BMI_BMM150)+=mag_bmm150.o
driver-$(CONFIG_ACCELGYRO_LSM6DSM)+=accelgyro_lsm6dsm.o stm_mems_common.o
driver-$(CONFIG_ACCELGYRO_LSM6DSO)+=accelgyro_lsm6dso.o stm_mems_common.o
driver-$(CONFIG_ACCEL_LIS2D_COMMON)+=accel_lis2dh.o stm_mems_common.o
driver-$(CONFIG_MAG_LIS2MDL)+=mag_lis2mdl.o
driver-$(CONFIG_SENSORHUB_LSM6DSM)+=sensorhub_lsm6dsm.o
driver-$(CONFIG_SYNC)+=sync.o
driver-$(CONFIG_ACCEL_LIS2DW_COMMON)+=accel_lis2dw12.o stm_mems_common.o
driver-$(CONFIG_ACCEL_LIS2DS)+=accel_lis2ds.o stm_mems_common.o
driver-$(CONFIG_ACCELGYRO_ICM426XX)+=accelgyro_icm426xx.o accelgyro_icm_common.o
driver-$(CONFIG_ACCELGYRO_ICM42607)+=accelgyro_icm42607.o accelgyro_icm_common.o

# BC1.2 Charger Detection Devices
driver-$(CONFIG_BC12_DETECT_MAX14637)+=bc12/max14637.o
driver-$(CONFIG_BC12_DETECT_MT6360)+=bc12/mt6360.o
driver-$(CONFIG_BC12_DETECT_PI3USB9201)+=bc12/pi3usb9201.o
driver-$(CONFIG_BC12_DETECT_PI3USB9281)+=bc12/pi3usb9281.o
driver-$(CONFIG_BC12_DETECT_RT1718S)+=bc12/rt1718s.o

# Gyrometers
driver-$(CONFIG_GYRO_L3GD20H)+=gyro_l3gd20h.o

# ALS drivers
driver-$(CONFIG_ALS_AL3010)+=als_al3010.o
driver-$(CONFIG_ALS_ISL29035)+=als_isl29035.o
driver-$(CONFIG_ALS_OPT3001)+=als_opt3001.o
driver-$(CONFIG_ALS_SI114X)+=als_si114x.o
driver-$(CONFIG_ALS_BH1730)+=als_bh1730.o
driver-$(CONFIG_ALS_TCS3400)+=als_tcs3400.o
driver-$(CONFIG_ALS_CM32183)+=als_cm32183.o
driver-$(CONFIG_ALS_CM36781)+=als_cm36781.o
driver-$(CONFIG_ALS_VEML3328)+=als_veml3328.o

# Barometers
driver-$(CONFIG_BARO_BMP280)+=baro_bmp280.o

# Batteries
driver-$(CONFIG_BATTERY_BQ20Z453)+=battery/bq20z453.o
driver-$(CONFIG_BATTERY_BQ27541)+=battery/bq27541.o
driver-$(CONFIG_BATTERY_BQ27621)+=battery/bq27621_g1.o
driver-$(CONFIG_BATTERY_MAX17055)+=battery/max17055.o
ifeq ($(HAS_MOCK_BATTERY),)
driver-$(CONFIG_BATTERY_SMART)+=battery/smart.o
endif
driver-$(CONFIG_BATTERY_BQ4050)+=battery/bq4050.o

# Battery charger ICs
driver-$(CONFIG_CHARGER_BQ25710)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_BQ25720)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_BQ25770)+=charger/bq25710.o
driver-$(CONFIG_CHARGER_ISL9237)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9238)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9238C)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_ISL9241)+=charger/isl9241.o
driver-$(CONFIG_CHARGER_ISL95522)+=charger/isl95522.o
driver-$(CONFIG_CHARGER_RAA489000)+=charger/isl923x.o
driver-$(CONFIG_CHARGER_RAA489110)+=charger/isl9241.o
driver-$(CONFIG_CHARGER_RT9490)+=charger/rt9490.o
driver-$(CONFIG_CHARGER_SM5803)+=charger/sm5803.o

# CEC drivers
driver-$(CONFIG_CEC_BITBANG)+=cec/bitbang.o
driver-$(CONFIG_CEC_IT83XX)+=cec/it83xx.o

# Fingerprint Sensors
include $(_driver_cur_dir)fingerprint/build.mk

# I/O expander
driver-$(CONFIG_IO_EXPANDER_CCGXXF)+=ioexpander/ccgxxf.o
driver-$(CONFIG_IO_EXPANDER_IT8801)+=ioexpander/it8801.o
driver-$(CONFIG_IO_EXPANDER_NCT38XX)+=ioexpander/ioexpander_nct38xx.o
driver-$(CONFIG_IO_EXPANDER_PCA9675)+=ioexpander/pca9675.o

driver-$(CONFIG_CTN730)+=nfc/ctn730.o

# 7-segment display
driver-$(CONFIG_MAX695X_SEVEN_SEGMENT_DISPLAY)+=led/max695x.o

# Voltage regulators
driver-$(CONFIG_REGULATOR_IR357X)+=regulator_ir357x.o

# Power Sourcing Equipment
driver-$(CONFIG_PSE_LTC4291)+=pse_ltc4291.o

# Temperature sensors
driver-$(CONFIG_TEMP_SENSOR_ADT7481)+=temp_sensor/adt7481.o
driver-$(CONFIG_TEMP_SENSOR_BD99992GW)+=temp_sensor/bd99992gw.o
driver-$(CONFIG_TEMP_SENSOR_EC_ADC)+=temp_sensor/ec_adc.o
driver-$(CONFIG_TEMP_SENSOR_F75303)+=temp_sensor/f75303.o
driver-$(CONFIG_TEMP_SENSOR_PCT2075)+=temp_sensor/pct2075.o
driver-$(CONFIG_TEMP_SENSOR_SB_TSI)+=temp_sensor/sb_tsi.o

# Touchpads
driver-$(CONFIG_TOUCHPAD_ELAN)+=touchpad_elan.o

# Thermistors
driver-$(CONFIG_THERMISTOR)+=temp_sensor/thermistor.o
driver-$(CONFIG_THERMISTOR_NCP15WB)+=temp_sensor/thermistor_ncp15wb.o

# Type-C port controller (TCPC) drivers
driver-$(CONFIG_USB_PD_TCPM_CCGXXF)+=tcpm/ccgxxf.o
driver-$(CONFIG_USB_PD_TCPM_TCPCI)+=tcpm/tcpci.o
driver-$(CONFIG_USB_PD_TCPM_FUSB302)+=tcpm/fusb302.o
ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
driver-y +=tcpm/ite_pd_intc.o
driver-$(CONFIG_USB_PD_TCPM_DRIVER_IT83XX)+=tcpm/it83xx.o
driver-$(CONFIG_USB_PD_TCPM_DRIVER_IT8XXX2)+=tcpm/it8xxx2.o
endif
driver-$(CONFIG_USB_PD_TCPM_ANX7447)+=tcpm/anx7447.o
driver-$(CONFIG_USB_PD_TCPM_PS8745)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8751)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8755)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8705)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8805)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_PS8815)+=tcpm/ps8xxx.o
driver-$(CONFIG_USB_PD_TCPM_TUSB422)+=tcpm/tusb422.o
driver-$(CONFIG_USB_PD_TCPM_RT1715)+=tcpm/rt1715.o
driver-$(CONFIG_USB_PD_TCPM_RT1718S)+=tcpm/rt1718s.o
driver-$(CONFIG_USB_PD_TCPM_NCT38XX)+=tcpm/nct38xx.o
driver-$(CONFIG_USB_PD_TCPM_RAA489000)+=tcpm/raa489000.o

# Type-C Retimer drivers
driver-$(CONFIG_USBC_RETIMER_ANX7483)+=retimer/anx7483.o
driver-$(CONFIG_USBC_RETIMER_ANX7452)+=retimer/anx7452.o
driver-$(CONFIG_USBC_RETIMER_INTEL_BB)+=retimer/bb_retimer.o
driver-$(CONFIG_USBC_RETIMER_KB8010)+=retimer/kb8010.o
driver-$(CONFIG_USBC_RETIMER_PS8802)+=retimer/ps8802.o
driver-$(CONFIG_USBC_RETIMER_PS8811)+=retimer/ps8811.o
driver-$(CONFIG_USBC_RETIMER_PS8818)+=retimer/ps8818.o

# USB mux high-level driver
driver-$(CONFIG_USBC_SS_MUX)+=usb_mux/usb_mux.o

# USB muxes
driver-$(CONFIG_USB_MUX_AMD_FP6)+=usb_mux/amd_fp6.o
driver-$(CONFIG_USB_MUX_ANX3443)+=usb_mux/anx3443.o
driver-$(CONFIG_USB_MUX_ANX7451)+=usb_mux/anx7451.o
driver-$(CONFIG_USB_MUX_IT5205)+=usb_mux/it5205.o
driver-$(CONFIG_USB_MUX_PS8743)+=usb_mux/ps8743.o
driver-$(CONFIG_USB_MUX_TUSB1044)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_TUSB1064)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_TUSB546)+=usb_mux/tusb1064.o
driver-$(CONFIG_USB_MUX_VIRTUAL)+=usb_mux/virtual.o

# Type-C Power Path Controllers (PPC)
driver-$(CONFIG_USBC_PPC_AOZ1380)+=ppc/aoz1380.o
driver-$(CONFIG_USBC_PPC_RT1718S)+=ppc/rt1718s.o
driver-$(CONFIG_USBC_PPC_RT1739)+=ppc/rt1739.o
driver-$(CONFIG_USBC_PPC_SN5S330)+=ppc/sn5s330.o
ifeq ($(CONFIG_USBC_PPC_NX20P3481)$(CONFIG_USBC_PPC_NX20P3483),y)
driver-y += ppc/nx20p348x.o
endif
driver-$(CONFIG_USBC_PPC_SYV682X)+=ppc/syv682x.o
driver-$(CONFIG_USBC_PPC_NX20P3483)+=ppc/nx20p348x.o
driver-$(CONFIG_USBC_PPC_KTU1125)+=ppc/ktu1125.o

# Switchcap
driver-$(CONFIG_LN9310)+=ln9310.o

# Wireless Power Chargers
driver-$(CONFIG_CPS8100)+=wpc/cps8100.o

# Power Management ICs
driver-$(CONFIG_MP2964)+=mp2964.o

# SOC Interface
driver-$(CONFIG_AMD_SB_RMI)+=sb_rmi.o
driver-$(CONFIG_AMD_STT)+=amd_stt.o
