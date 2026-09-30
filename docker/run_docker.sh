#!/bin/bash
# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Run a command in the EC firmware container, or a shell if none is given.
# compose.yaml configures the container; this script adds --rm, which a
# Compose file cannot express.

exec docker compose -f "$(dirname "${BASH_SOURCE[0]}")/compose.yaml" \
  run --rm shell "$@"
