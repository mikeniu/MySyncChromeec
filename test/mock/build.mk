# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# See test/mock/README.md for more information.

test_mock-$(HAS_MOCK_ADC) += adc_mock.o
test_mock-$(HAS_MOCK_BATTERY) += battery_mock.o
test_mock-$(HAS_MOCK_CHARGE_MANAGER) += charge_manager_mock.o
test_mock-$(HAS_MOCK_CLOCK) += clock_mock.o
test_mock-$(HAS_MOCK_FPSENSOR) += fpsensor_mock.o
test_mock-$(HAS_MOCK_FPSENSOR_CRYPTO) += fpsensor_crypto_mock.o
test_mock-$(HAS_MOCK_FPSENSOR_DETECT) += fpsensor_detect_mock.o
test_mock-$(HAS_MOCK_FPSENSOR_STATE) += fpsensor_state_mock.o
test_mock-$(HAS_MOCK_MKBP_EVENTS) += mkbp_events_mock.o
test_mock-$(HAS_MOCK_OTPI) += otpi_mock.o
test_mock-$(HAS_MOCK_ROLLBACK) += rollback_mock.o
test_mock-$(HAS_MOCK_ROLLBACK_LATEST) += rollback_latest_mock.o
test_mock-$(HAS_MOCK_TCPC) += tcpc_mock.o
test_mock-$(HAS_MOCK_TCPM) += tcpm_mock.o
test_mock-$(HAS_MOCK_TCPCI_I2C) += tcpci_i2c_mock.o
test_mock-$(HAS_MOCK_TIMER) += timer_mock.o
test_mock-$(HAS_MOCK_USB_MUX) += usb_mux_mock.o
test_mock-$(HAS_MOCK_USB_PE_SM) += usb_pe_sm_mock.o
test_mock-$(HAS_MOCK_USB_TC_SM) += usb_tc_sm_mock.o
test_mock-$(HAS_MOCK_USB_PD_DPM) += usb_pd_dpm_mock.o
test_mock-$(HAS_MOCK_DP_ALT_MODE) += dp_alt_mode_mock.o
test_mock-$(HAS_MOCK_USB_PRL) += usb_prl_mock.o
test_mock-$(HAS_MOCK_USB_PE_DRP_SM) += usb_pe_drp_sm_mock.o
