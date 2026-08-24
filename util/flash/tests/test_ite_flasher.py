import unittest
from unittest.mock import MagicMock, patch
from util.flash.ite_flasher import IteUartFlasher
from util.flash.servo_utils import ServoDriver

class TestIteFlasher(unittest.TestCase):
    def setUp(self):
        self.servo = MagicMock(spec=ServoDriver)
        self.args = MagicMock()
        self.args.image = "/tmp/image.bin"
        self.args.baud = None
        self.args.verify = True
        self.args.dry_run = False
        self.flasher = IteUartFlasher(self.servo, self.args)

    @patch("time.sleep")
    def test_prepare_dut_with_ccd(self, mock_sleep):
        def mock_get(key, default=""):
            if key == "servo_type":
                return "ccd_cr50"
            return MagicMock()
        self.servo.get.side_effect = mock_get
        
        self.flasher.prepare_dut()
        
        self.servo.set.assert_any_call("cr50_uart_cmd", "sysrst assert")
        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst assert")
        self.servo.set.assert_any_call("cr50_i2c_ctrl", "ite_debugger_mode")

    def test_prepare_dut(self):
        def mock_get(key, default=""):
            if key == "servo_type":
                return "servo_v4"
            return default
        self.servo.get.side_effect = mock_get
        
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("cold_reset", "on")
        self.servo.set.assert_any_call("ec_uart_en", "on")

    def test_prepare_dut_with_baud_override(self):
        self.args.baud = "51200"
        self.flasher.prepare_dut()
        self.servo.set.assert_any_call("ec_uart_baudrate", "51200")

    @patch("subprocess.run")
    def test_execute_flash(self, mock_run):
        self.servo.get.return_value = "/dev/pts/1"
        self.flasher.execute_flash()
        mock_run.assert_called_once_with(
            ["itecomdbgr", "-d", "/dev/pts/1", "-f", "/tmp/image.bin", "-b", "115200"],
            check=True
        )

    @patch("time.sleep")
    def test_cleanup_dut_with_ccd(self, mock_sleep):
        def mock_get(key, default=""):
            if key == "servo_type":
                return "ccd_cr50"
            return default
        self.servo.get.side_effect = mock_get
        
        self.flasher.cleanup_dut()
        self.servo.set.assert_any_call("cr50_uart_cmd", "sysrst deassert")
        self.servo.set.assert_any_call("gsc_uart_cmd", "sysrst deassert")

    def test_cleanup_dut(self):
        def mock_get(key, default=""):
            if key == "servo_type":
                return "servo_v4"
            return default
        self.servo.get.side_effect = mock_get
        
        self.flasher.cleanup_dut()
        self.servo.set.assert_called_once_with("cold_reset", "off")

    @patch.object(IteUartFlasher, 'prepare_dut')
    @patch.object(IteUartFlasher, 'execute_flash')
    @patch.object(IteUartFlasher, 'cleanup_dut')
    def test_run_calls_proper_sequence(self, mock_cleanup, mock_exec, mock_prep):
        self.flasher.run()
        mock_prep.assert_called_once()
        mock_exec.assert_called_once()
        mock_cleanup.assert_called_once()
