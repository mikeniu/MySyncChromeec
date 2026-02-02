# Copyright 2026 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

host-util-bin-cxx-y += elaniap
elaniap-objs = elan/iaptool.o elan/iapcontrol.o elan/filecontrol.o
$(out)/util/elaniap: HOST_CXXFLAGS+=-Wno-vla-cxx-extension
$(out)/util/elaniap: HOST_CFLAGS+=$(shell $(HOST_PKG_CONFIG) --cflags hidapi-hidraw)
$(out)/util/elaniap: HOST_LDFLAGS+=$(shell $(HOST_PKG_CONFIG) --libs hidapi-hidraw)
