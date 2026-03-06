# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Define zmake projects for rtk_flame."""

# This is a stub Realtek base project that triggers a build of the
# the rts5915_flash_upload.bin binary from zephyr/chip/rtk_flame
# target.
#
# After running "zmake build rtk_flame" the correct Realtek monitor
# binary is created at:
#     build/zephyr/rtk_flame/build-singleimage/rts5912_flash_upload.bin
# Use the above artifact to update the servod docker.
#
# The Zephyr binaries listed below should *not* be used.
#    build/zephyr/rtk_flame/build-singleimage/zephyr/zephyr.bin
#    build/zephyr/rtk_flame/output/ec.bin

register_raw_project(
    project_name="rtk_flame",
    zephyr_board="realtek/rts5912",
    modules=["ec", "cmsis_6"],
)
