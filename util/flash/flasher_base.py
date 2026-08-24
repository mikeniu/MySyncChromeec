import abc
import logging
import subprocess
from .servo_utils import ServoDriver

class BaseEcFlasher(abc.ABC):
    """
    Abstract base class defining the Template Method for EC flashing.
    """
    def __init__(self, servo: ServoDriver, args):
        self.servo = servo
        self.args = args  # CLI arguments object mapping config overrides
        self._logger = logging.getLogger(self.__class__.__name__)

    @abc.abstractmethod
    def prepare_dut(self):
        """Put the DUT into a flashable state (e.g., assert resets)."""
        pass

    @abc.abstractmethod
    def execute_flash(self):
        """Invoke the target flashing tool."""
        pass

    @abc.abstractmethod
    def cleanup_dut(self):
        """Restore the DUT to a bootable state."""
        pass

    def verify_flash(self):
        """Optional sequence to verify the flashed image."""
        if not getattr(self.args, "verify", False):
            self._logger.info("Verification skipped (--noverify).")
            return
        self._logger.info("Verifying flash... (Not implemented in base)")

    def run(self):
        """Unified Code Flow Path."""
        board = getattr(self.args, "board", "")
        self._logger.info(f"Starting EC flash sequence for board: {board}")
        try:
            self._logger.info("Applying Deep Cr50/GSC Hacks")
            # Ensure cr50-update service is stopped.
            # Avoid using 'sudo -n' to allow interactive terminals to prompt for password,
            # while CI/CD environments should have NOPASSWD configured.
            
            # Check if initctl is available
            if subprocess.run(["which", "initctl"], capture_output=True).returncode == 0:
                try:
                    subprocess.run(["sudo", "initctl", "stop", "cr50-update"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
                except Exception:
                    pass
                    
            if subprocess.run(["which", "systemctl"], capture_output=True).returncode == 0:
                try:
                    subprocess.run(["sudo", "systemctl", "stop", "cr50-update"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
                except Exception:
                    pass

            # Drop write protection securely
            if self.servo:
                try:
                    self.servo.set("fw_wp_state", "force_off")
                    self._logger.info("Successfully forced fw_wp_state off.")
                except Exception as e:
                    self._logger.warning(f"Could not force_off fw_wp_state: {e}")

            self.prepare_dut()
            
            try:
                self.execute_flash()
            except FileNotFoundError as e:
                self._logger.info(f"Binary missing ({e}), attempting auto-build recovery...")
                import os
                ec_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
                # Fallback build command as specified
                subprocess.run(["make", "-j", "utils-host"], cwd=ec_dir, check=True)
                # Try running again after the build
                os.environ["PATH"] = f"{os.path.join(ec_dir, 'build', 'host', 'util')}:{os.environ.get('PATH', '')}"
                self.execute_flash()
                
            self.verify_flash()

            self._logger.info("EC Flash completed successfully.")
        except Exception as e:
            self._logger.error(f"EC Flash failed: {e}")
            raise
        finally:
            self.cleanup_dut()
            if getattr(self.args, "try_apshutdown", False):
                self._logger.info("Attempting to shut down the AP...")
                if self.servo:
                    try:
                        self.servo.set("ec_uart_regexp", "None")
                        count = getattr(self.args, "try_apshutdown_count", 16)
                        import time
                        for _ in range(count):
                            self.servo.set("ec_uart_cmd", "apshutdown")
                            time.sleep(1)
                    except Exception as e:
                        self._logger.warning(f"Error during AP shutdown attempt: {e}")
