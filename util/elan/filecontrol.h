// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>
#include <fstream>
#include <iostream>
using namespace std;

int getBinary(string filePath, unsigned char *pBinary, uint32_t *pLength);
