#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 the exult-3ds authors
# Rebuild exult.3dsx from source. Needs Docker (Docker Desktop on Windows: run from WSL or Git Bash).
#   ./build.sh            -> out/exult.3dsx + out/data/*
# Pinned to the same Exult / SDL3 commits the shipped binary was built from.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
W="$HERE/work"; mkdir -p "$W" "$HERE/out/data"
EXULT_COMMIT=$(cat "$HERE/patches/EXULT_COMMIT")
SDL3_COMMIT=$(cat "$HERE/patches/SDL3_COMMIT")

fetch() { # repo dir commit
  [ -d "$W/$2/.git" ] || git clone https://github.com/$1.git "$W/$2"
  git -C "$W/$2" fetch --depth 1 origin "$3" && git -C "$W/$2" checkout -q -f "$3"
}
fetch exult/exult exult "$EXULT_COMMIT"
fetch libsdl-org/SDL sdl3 "$SDL3_COMMIT"
[ -d "$W/vorbis" ] || git clone --depth 1 -b v1.3.7 https://github.com/xiph/vorbis.git "$W/vorbis"

git -C "$W/sdl3" apply "$HERE/patches/sdl3-n3ds-virtual-input.patch"
git -C "$W/exult" apply "$HERE/patches/exult-3ds.patch"
mkdir -p "$W/exult/3ds"
cp "$HERE/3ds/n3ds_platform.cc" "$HERE/3ds/n3ds_bottom.cc" "$HERE/3ds/n3ds_frontend.cc" "$HERE/3ds/n3ds_font.h" "$HERE/3ds/n3ds_stereo.cc" "$HERE/3ds/n3ds_stereo.h" "$HERE/3ds/manual_text.h" "$HERE/3ds/click3ds.h" "$HERE/3ds/icon.png" "$W/exult/3ds/"
cp "$HERE/3ds/Makefile.3ds" "$W/exult/"
sed -n '1,/^OBJS:=/p' "$W/exult/Makefile.common" > "$W/exult/3ds/objs.mk"

# 1) Host build of Exult's 'expack' tool + generated data (exult.flx etc. and their headers)
rm -rf "$W/host" && cp -r "$W/exult" "$W/host"
docker run --rm -v "$W/host":/src -v "$HERE/3ds":/kit -w /src devkitpro/devkitarm bash -c \
  "cp /kit/host-expack.mk . && make -f host-expack.mk expack && make -f host-expack.mk \$(grep -o 'data/[a-z_/]*\.flx\|data/[a-z_]*\.vga' Makefile.common | sort -u)"
(cd "$W/host" && find data -name '*_flx.h' -o -name '*_vga.h' -o -name '*.flx' -o -name '*.vga' | while read f; do cp "$f" "$W/exult/$f"; done)

# 2) 3DS deps + Exult
docker run --rm -v "$W":/w devkitpro/devkitarm bash -ec '
  T=/opt/devkitpro/cmake/3DS.cmake; P=/opt/devkitpro/portlibs/3ds
  cmake -S /w/sdl3 -B /w/b-sdl3 -DCMAKE_TOOLCHAIN_FILE=$T -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=/w/deps
  cmake --build /w/b-sdl3 -j$(nproc) && cmake --install /w/b-sdl3
  cmake -S /w/vorbis -B /w/b-vorbis -DCMAKE_TOOLCHAIN_FILE=$T -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=/w/deps -DOGG_INCLUDE_DIR=$P/include -DOGG_LIBRARY=$P/lib/libogg.a
  cmake --build /w/b-vorbis -j$(nproc) && cmake --install /w/b-vorbis
  cd /w/exult && make -f Makefile.3ds DEPS=/w/deps -j$(nproc)'

cp "$W/exult/exult.3dsx" "$HERE/out/"
for f in exult.flx exult_bg.flx exult_si.flx fonts_original.vga fonts_serif.vga shortcutbar.vga midisfx.flx; do
  cp "$W/exult/data/$f" "$HERE/out/data/"; done
# Modern fonts: U7 Pixel Sans (hand-drawn for the 3DS screen, fonts/u7pixel_glyphs.py)
# over Exult's serif font set. Prebuilt; regenerate with:
#   python3 fonts/make_fonts.py <exult>/data/fonts_serif.vga pixel fonts/fonts_modern.vga
cp "$HERE/fonts/fonts_modern.vga" "$HERE/out/data/"
echo "Built: $HERE/out/exult.3dsx"
