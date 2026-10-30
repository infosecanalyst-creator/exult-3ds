#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 the exult-3ds authors
"""Regenerate manual_text.h from manual.txt (the in-game manual)."""
import pathlib, sys
here = pathlib.Path(__file__).parent
t = (here / "manual.txt").read_text()
assert ')MANUAL"' not in t
(here / "manual_text.h").write_text('// SPDX-License-Identifier: GPL-2.0-or-later\n// Copyright (C) 2026 the exult-3ds authors\n// Generated from 3ds/manual.txt (the in-game manual). Regenerate with build/3ds/make_manual_h.py.\n#pragma once\n'
    'static const char* const N3DS_MANUAL_BUILTIN = R"MANUAL(' + t + ')MANUAL";\n')
