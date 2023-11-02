#!/bin/bash

# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

DUT="${1:-dut1}"

msg-run() {
	echo "> $*"
	"$@"
}

ssh-run() {
	msg-run ssh "${DUT}" -- "$@"
}

ssh-run touch /mnt/stateful_partition/.disable_fp_updater
msg-run scp build/bloonchipper/ec.bin "${DUT}":/tmp/druid.bin
ssh-run flash_fp_mcu /tmp/druid.bin
ssh-run ectool --name=cros_fp version
ssh-run bio_wash --factory_init
ssh-run ectool --name=cros_fp rollbackinfo
