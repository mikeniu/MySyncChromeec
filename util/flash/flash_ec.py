#!/usr/bin/env vpython3
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
Provides a unified interface to flash various EC targets across ChromeOS infrastructure.
"""
import argparse
import logging
import sys
import time

from util.flash.servo_utils import ServoDriver
from util.flash.factory import FlasherFactory

def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description="Script to flash EC firmware.")
    
    # Core settings
    parser.add_argument("--board", required=True, help="Target board name")
    parser.add_argument("--image", required=True, help="Path to EC binary image")
    parser.add_argument("--port", default=9999, type=int, help="Servod port to connect to")
    parser.add_argument("--dut_ip", default="", help="IP address of DUT (for net-based flashing)")
    parser.add_argument("--read", default="", help="Pathname to store read EC firmware image")
    parser.add_argument("--offset", default="0", help="SPI offset")
    
    # Overrides (to avoid probing)
    parser.add_argument("--chip", default="", help="Override EC chip type detection")
    parser.add_argument("--ec_source", default="", help="Path to EC source repository to dynamically deduce board variables")
    parser.add_argument("--interface", choices=["uart", "spi", "i2c", "jtag", "dfu", "openocd"], help="Override physical flashing interface")
    parser.add_argument("--baud", help="Override baud rate for UART tools")
    parser.add_argument("--bitbang_rate", dest="baud", help="Alias for --baud (backwards compatibility with bash flash_ec)")
    parser.add_argument("--mchp_loader", default="", help="Microchip bootloader file override")
    parser.add_argument("--token_db", default="", help="Pathname to the EC tokenized database")
    
    # Behavioral Modifiers
    parser.add_argument("--zephyr", action="store_true", help="Image is a Zephyr build")
    parser.add_argument("--ro", action="store_true", help="Flash only the RO section")
    parser.add_argument("--verify", action="store_true", default=True, help="Verify after flash (default)")
    parser.add_argument("--noverify", action="store_false", dest="verify", help="Skip verification")
    parser.add_argument("--no_preserve", action="store_true", help="Do not preserve protected flash sections")
    parser.add_argument("--try_apshutdown", action="store_true", help="Attempt to shut down AP before flashing")
    parser.add_argument("--try_apshutdown_count", type=int, default=16, help="Retries for AP shutdown")
    
    parser.add_argument("--raiden", action="store_true", help="Use raiden debugging")
    parser.add_argument("--servo_micro_uart_rx_rework", action="store_true", help="Servo micro hardware rework flag")
    parser.add_argument("--use_i2c_pseudo", action="store_true", help="Use I2C pseudo interfaces")
    parser.add_argument("--usbc_charging", action="store_true", help="Enable USBC charging during recovery")
    parser.add_argument("--verify_ec_type", action="store_true", help="Strictly verify the EC chip matches board")
    
    # Debug / Execution Context
    parser.add_argument("--dry_run", action="store_true", help="Print commands without executing them")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose debug logging")
    parser.add_argument("--logfile", default="", help="Optional path to emit execution logs")
    parser.add_argument("--timeout", type=int, default=600, help="Flashing tool execution timeout in seconds")

    return parser.parse_args(argv)

def setup_logging(args):
    log_level = logging.DEBUG if args.verbose else logging.INFO
    handlers = [logging.StreamHandler(sys.stdout)]
    if args.logfile:
        handlers.append(logging.FileHandler(args.logfile))
        
    logging.basicConfig(level=log_level, format="%(asctime)s [%(name)s] %(levelname)s: %(message)s", handlers=handlers)

def main(argv=None):
    args = parse_arguments(argv)
    setup_logging(args)
    logger = logging.getLogger("flash_ec")
    
    start_time = time.time()
    logger.info(f"Initiating EC flash tool implementation (Python). Target: {args.board}")
    
    try:
        servo = ServoDriver(port=args.port, dut_ip=args.dut_ip, dry_run=args.dry_run)
        
        # FlasherFactory dynamically interprets args, overrides, and servo outputs
        flasher = FlasherFactory.create(servo, args)
        
        # Executes Template Method: Prepare -> Flash -> Verify -> Cleanup
        flasher.run()
        
    except Exception as e:
        logger.error(f"Fatal error during execution: {e}")
        if args.verbose:
            import traceback
            logger.error(traceback.format_exc())
        sys.exit(1)
        
    elapsed = time.time() - start_time
    logger.info(f"Execution completed in {elapsed:.2f} seconds.")

if __name__ == "__main__":
    main()
