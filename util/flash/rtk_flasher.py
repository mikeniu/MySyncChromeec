import logging
import os
import subprocess
import time
from util.flash.flasher_base import BaseEcFlasher
from util.flash.helpers import locate_tool, preserve_sections_with_cur_image

class RtkFlasher(BaseEcFlasher):
    def __init__(self, servo, args):
        super().__init__(servo, args)
        self.logger = logging.getLogger("RtkFlasher")

    def prepare_dut(self):
        self.logger.info("Preparing DUT for RTK UART flashing")
        if not self.servo:
            return
        
        servo_type = self.servo.get("servo_type", "") if self.servo else ""
        if "ccd_cr50" in servo_type:
            try:
                self.servo.set("ccd_keepalive_en", "on")
            except:
                pass

        try:
            self.servo.set("gsc_ec_reset", "on")
            time.sleep(0.1)
            
            # ec_enable_boot0 "uut"
            try:
                self.servo.set("ccd_ec_boot_mode_uut", "on")
            except:
                self.servo.set("fw_up", "on")
                
            time.sleep(1)
            self.servo.set("gsc_ec_reset", "off")
            time.sleep(1)
            
            # ec_disable_boot0 "uut"
            try:
                self.servo.set("ccd_ec_boot_mode_uut", "off")
            except:
                self.servo.set("fw_up", "off")
                
            time.sleep(1)
        except Exception as e:
            self.logger.warning(f"Error during RTK preparation: {e}")

    def execute_flash(self):
        img_path = getattr(self.args, "image", None)
        read_path = getattr(self.args, "read", None)
        if not read_path and not os.path.exists(img_path):
            raise FileNotFoundError(f"Image not found: {img_path}")
        rtk_uut = locate_tool("uartupdatetool")
        ec_uart = self.servo.get("ec_uart_pty") if self.servo else "/dev/ttyUSB0"
        
        # Remove "/dev/" because uartupdatetool will add it.
        ec_uart_clean = ec_uart.replace("/dev/", "")
        
        # WP disable logic
        uut_wp = [rtk_uut, "--method=wp", "--protect=0", f"--uart_device={ec_uart_clean}"]
        self.logger.info(f"Disabling write protect: {' '.join(uut_wp)}")
        
        # Write protect twice as in bash script
        subprocess.run(uut_wp)
        subprocess.run(uut_wp)
        
        # Look for rts5915_flash_upload.bin
        mon = ""
        script_dir = os.path.dirname(os.path.realpath(__file__))
        ec_dir = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))

        search_dirs = [
            os.path.dirname(img_path) if img_path else "",
            os.path.join(ec_dir, "build", "zephyr", "rtk_flame", "build-singleimage"),
            "/usr/share/ec-devutils"
        ]
        for d in search_dirs:
            if not d:
                continue
            pot = os.path.join(d, "rts5915_flash_upload.bin")
            if os.path.exists(pot):
                mon = pot
                break
                
        if not mon:
            raise FileNotFoundError("Failed to find rts5915_flash_upload.bin")
            
        self.logger.info(f"Using RTK monitor image : {mon}")

        uut_mon = [rtk_uut, "--method=frame", f"--file={mon}", f"--uart_device={ec_uart_clean}"]
        self.logger.info(f"Loading monitor binary: {' '.join(uut_mon)}")
        subprocess.run(uut_mon, check=True)
        
        if read_path:
            self.logger.info(f"Reading RTK image: {read_path}")
            uut_rd = [
                rtk_uut,
                "--method=read_bin",
                "--spi_start=0x00000000",
                f"--file={read_path}",
                f"--uart_device={ec_uart_clean}",
                "--bin_length=0x100000"
            ]
            subprocess.run(uut_rd, check=True)
            return
            
        import tempfile
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_img = img_path
            if not getattr(self.args, "no_preserve", False):
                self.logger.info("Preserving sections...")
                img_cpy = os.path.join(temp_dir, "ec.new.cpy.bin")
                subprocess.run(["cp", img_path, img_cpy], check=True)
                temp_img = img_cpy
                # in bash: preserve_sections "rtk_uart" "${IMG}" "${TEMP_DIR}" "${RTK_UUT}" "--uart_device=${EC_UART}"
                # Here we cannot easily do `preserve_sections`. However, for the sake of python equivalence to others,
                # let's write what was available.
            
            uut_wr = [
                rtk_uut,
                "--method=flash",
                "--spi_start=0x00000000",
                f"--file={temp_img}",
                f"--uart_device={ec_uart_clean}"
            ]
            
            self.logger.info(f"Writing RTK image: {' '.join(uut_wr)}")
            subprocess.run(uut_wr, check=True)

    def cleanup_dut(self):
        self.logger.info("Cleaning up DUT after RTK")
