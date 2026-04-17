"""
Mapping of Hardware Component Types to EC Firmware files and subsystems.
Used to associate code changes with physical hardware impact.
"""

import re
import fnmatch

# List of component types relevant to EC firmware from the official database.
RELEVANT_COMPONENT_TYPES = [
    "Accelerometer/IMU",
    "Ambient Light Sensor",
    "Battery",
    "Battery Charger Integrated Component",
    "CPU",
    "EC",
    "Fingerprint MCU",
    "Fuel Gauge",
    "HPS (Human Presence Sensor)",
    "Keyboard MCU",
    "Lid Sensor (Hall Effect)",
    "PMIC",
    "Proximity(SAR) Sensor",
    "Secure Element",
    "Smart Speaker Amplifier",
    "Stylus (Pen)",
    "TPM",
    "Thermal IC",
    "Touchpad Controller",
    "USB-C Power Delivery Controller",
    "Wifi / Bluetooth",
    "WPC Qi PTx (Power Transmitter)",
]

# Mapping of Component Type -> List of glob patterns for relevant files.
COMPONENT_FILE_PATTERNS = {
    "Accelerometer/IMU": [
        "common/motion_sense*",
        "common/motion_lid*",
        "common/motion_sense_fifo*",
        "driver/accel*",
        "driver/stm_mems*",
        "driver/mag*",
    ],
    "Ambient Light Sensor": [
        "driver/als*",
        "common/als*",
    ],
    "Battery": [
        "common/battery*",
        "driver/battery*",
        "common/virtual_battery*",
        "common/battery_info*",
    ],
    "Battery Charger Integrated Component": [
        "common/charger*",
        "common/charge*",
        "driver/charger*",
        "common/usb_charger*",
        "common/peripheral_charger*",
    ],
    "CPU": [
        "common/chipset*",
        "power/*",
        "common/host_command*",
        "common/ap_hang_detect*",
        "common/ap_reset_log*",
        "common/acpi*",
        "common/port80*",
        "common/throttle_ap*",
    ],
    "EC": [
        "core/*",
        "common/main.c",
        "common/task.c",
        "common/system*",
        "common/hooks*",
        "common/flash*",
        "common/rollback*",
        "common/version*",
        "common/console*",
        "common/panic*",
        "common/uptime*",
    ],
    "Fingerprint MCU": [
        "common/fpsensor*",
        "driver/fingerprint*",
    ],
    "Fuel Gauge": [
        "common/battery_fuel_gauge*",
        "driver/battery*",
    ],
    "HPS (Human Presence Sensor)": [
        "driver/hps*",
    ],
    "Keyboard MCU": [
        "common/keyboard*",
        "driver/keyboard*",
        "common/rgb_keyboard*",
    ],
    "Lid Sensor (Hall Effect)": [
        "common/lid_switch*",
        "common/backlight_lid*",
        "common/lid_angle*",
    ],
    "PMIC": [
        "driver/pmic*",
        "common/pmic*",
        "common/regulator*",
    ],
    "Proximity(SAR) Sensor": [
        "driver/proximity*",
        "common/body_detect*",
        "common/body_detection*",
    ],
    "Secure Element": [
        "common/secure_element*",
    ],
    "Smart Speaker Amplifier": [
        "driver/amplifier*",
        "driver/speaker*",
    ],
    "Stylus (Pen)": [
        "common/stylus*",
        "driver/stylus*",
    ],
    "TPM": [
        "common/tpm*",
        "driver/tpm*",
    ],
    "Thermal IC": [
        "common/thermal*",
        "common/temp_sensor*",
        "driver/temp_sensor*",
        "common/dptf*",
    ],
    "Touchpad Controller": [
        "common/touchpad*",
        "driver/touchpad*",
    ],
    "USB-C Power Delivery Controller": [
        "common/usb_pd*",
        "common/usbc*",
        "driver/tcpm*",
        "driver/ppc*",
        "common/typec*",
        "driver/usb_mux*",
        "driver/retimer*",
    ],
    "Wifi / Bluetooth": [
        "common/wireless*",
        "driver/wireless*",
    ],
    "WPC Qi PTx (Power Transmitter)": [
        "driver/wpc*",
        "common/peripheral_charger*",
    ],
}

def get_components_for_file(file_path):
    """
    Given a file path, return a list of associated component types.
    """
    associated_components = []
    for component, patterns in COMPONENT_FILE_PATTERNS.items():
        for pattern in patterns:
            if fnmatch.fnmatch(file_path, pattern) or fnmatch.fnmatch(os.path.basename(file_path), pattern):
                associated_components.append(component)
                break
    return associated_components

def get_components_for_commit(file_list):
    """
    Given a list of files in a commit, return a set of unique component types.
    """
    components = set()
    for f in file_list:
        components.update(get_components_for_file(f))
    return components
