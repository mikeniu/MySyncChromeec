import subprocess
from util.flash.flasher_base import BaseEcFlasher

class Stm32UartFlasher(BaseEcFlasher):
    """STM32 UART Flasher using stm32mon."""
    def prepare_dut(self):
        self._logger.info("Preparing STM32 DUT (Cold Reset + Boot0 asserts).")
        if "c2d2" in self.servo.get("servo_type"):
            self.servo.set("gsc_ec_reset", "on")
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("gsc_ec_reset", "off")
        else:
            self.servo.set("ec_boot_mode", "uut")
            self.servo.set("cold_reset", "on")
            self.servo.set("cold_reset", "off")

    def execute_flash(self):
        pty = self.servo.get("ec_uart_pty")
        cmd = ["stm32mon", "-d", pty, "-w", self.args.image]
        if self.args.ro:
            cmd.append("-r")
        if self.args.dry_run:
            self._logger.info(f"DRY RUN TOOL: {' '.join(cmd)}")
            return
            
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        self._logger.info("Cleaning up STM32 DUT state.")
        self.servo.set("ec_boot_mode", "normal")
        self.servo.set("cold_reset", "on")
        self.servo.set("cold_reset", "off")


class Stm32DfuFlasher(BaseEcFlasher):
    """STM32 DFU Flasher using dfu-util."""
    def prepare_dut(self):
        self._logger.info("Executing DFU transition for STM32 (Not implemented precisely).")

    def execute_flash(self):
        cmd = ["dfu-util", "-a", "0", "-s", "0x08000000:leave", "-D", self.args.image]
        if self.args.dry_run:
            self._logger.info(f"DRY RUN TOOL: {' '.join(cmd)}")
            return
        subprocess.run(cmd, check=True)

    def cleanup_dut(self):
        pass
