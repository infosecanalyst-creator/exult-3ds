# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 the exult-3ds authors
include Makefile.common
SRC:=.
CXX:=g++
CXXFLAGS:=-std=c++17 -O1 -Wall -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare -Wno-deprecated-declarations -Wno-misleading-indentation -Wno-maybe-uninitialized   # EX-14: no -w; only upstream's known-noisy classes are muted
CPPFLAGS:=-DEXULT_DATADIR=\"data\" -DHAVE_SYS_TYPES_H=1 -DVERSION=\"$(VERSION)\" -I. -Iheaders -Ifiles -Ifiles/zip -Iconf -Iimagewin -Ishapes -Iusecode -Itools -Idata
all: data/exult_flx.h
