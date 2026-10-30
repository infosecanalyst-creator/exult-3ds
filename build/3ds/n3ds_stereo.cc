// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 the exult-3ds authors
/*
 * Exult for New 3DS - stereoscopic 3D. See n3ds_stereo.h.
 *
 * Depth units: a quarter of a lift (one screen pixel of height, since a lift
 * raises a shape by 4 pixels). 0 = ground, 254 = very high, 255 = windows,
 * text and other UI, which sit on the screen plane.
 *
 * GPL v2 (same as Exult).
 */
#include "n3ds_stereo.h"

#include "gamewin.h"
#include "ibuf8.h"
#include "iwin8.h"

#include <3ds.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace n3ds {
	bool depth_active = false;
}
bool n3ds_game_on_top();    // n3ds_bottom.cc: the game was swapped back to the top screen

namespace {
	constexpr uint8_t DEPTH_UI = 255;

	std::vector<uint8_t> depth;
	int                  dw = 0, dh = 0;
	bool                 in_world = false;
	int                  cur_lift = 0, cur_height = 0;
	bool                 need_repaint = false;

	// How far the scene sits behind / in front of the screen, in pixels of
	// parallax at full slider. Ground sits ~5 px behind the screen, lift 10
	// is on the screen plane and higher things (upper floors, roofs, tree
	// tops) come out of it.
	constexpr float GROUND_PARALLAX = 5.0f;
	constexpr float PARALLAX_PER_Q  = GROUND_PARALLAX / 40.0f;

	Image_buffer8* target() {
		Game_window* gw = Game_window::get_instance();
		if (!gw || !gw->get_win()) {
			return nullptr;
		}
		return gw->get_win()->get_ib8();
	}

	bool ensure_buffer() {
		Game_window* gw = Game_window::get_instance();
		if (!gw) {
			return false;
		}
		const int w = gw->get_width(), h = gw->get_height();
		if (w <= 0 || h <= 0) {
			return false;
		}
		if (w != dw || h != dh) {
			dw = w;
			dh = h;
			depth.assign(static_cast<size_t>(w) * h, 0);
		}
		return true;
	}

	// Intersect a span/rect with the buffer and the target's clip rectangle.
	bool clip_rect(const Image_buffer8* win, int& x, int& y, int& w, int& h) {
		int cx, cy, cw, ch;
		const_cast<Image_buffer8*>(win)->get_clip(cx, cy, cw, ch);
		int x0 = std::max({x, cx, 0}), y0 = std::max({y, cy, 0});
		int x1 = std::min({x + w, cx + cw, dw}), y1 = std::min({y + h, cy + ch, dh});
		if (x1 <= x0 || y1 <= y0) {
			return false;
		}
		x = x0;
		y = y0;
		w = x1 - x0;
		h = y1 - y0;
		return true;
	}

	inline uint8_t world_depth(int yoff, int y) {
		const int base = cur_lift * 4;
		int       z    = base + std::max(0, yoff - y);
		z              = std::min(z, base + cur_height * 4);
		return static_cast<uint8_t>(std::clamp(z, 0, 254));
	}

	inline bool stamping(const Image_buffer8* win) {
		return n3ds::depth_active && !depth.empty() && win == target();
	}
}    // namespace

namespace n3ds {
	void depth_begin_world(int x, int y, int w, int h) {
		in_world   = true;
		cur_lift   = 0;
		cur_height = 0;
		if (!depth_active || !ensure_buffer()) {
			return;
		}
		x = std::max(x, 0);
		y = std::max(y, 0);
		w = std::min(x + w, dw) - x;
		h = std::min(y + h, dh) - y;
		for (int r = 0; r < h; r++) {
			std::memset(&depth[static_cast<size_t>(y + r) * dw + x], 0, w > 0 ? w : 0);
		}
	}

	void depth_end_world() {
		in_world   = false;
		cur_lift   = 0;
		cur_height = 0;
	}

	void depth_object(int lift, int height3d) {
		cur_lift   = lift;
		cur_height = height3d;
	}

	void depth_stamp_rle(const Image_buffer8* win, int xoff, int yoff, const unsigned char* in) {
		if (!stamping(win) || !in) {
			return;
		}
		int scanlen;
		while ((scanlen = in[0] | (in[1] << 8)) != 0) {
			const int   encoded = scanlen & 1;
			const int   len     = scanlen >> 1;
			const short scanx   = static_cast<short>(in[2] | (in[3] << 8));
			const short scany   = static_cast<short>(in[4] | (in[5] << 8));
			in += 6;
			if (!encoded) {
				in += len;
			} else {
				for (int b = 0; b < len;) {
					unsigned char bcnt   = *in++;
					const int     repeat = bcnt & 1;
					bcnt >>= 1;
					in += repeat ? 1 : bcnt;
					b += bcnt;
				}
			}
			int x = xoff + scanx, y = yoff + scany, w = len, h = 1;
			if (clip_rect(win, x, y, w, h)) {
				std::memset(&depth[static_cast<size_t>(y) * dw + x], in_world ? world_depth(yoff, y) : DEPTH_UI, w);
			}
		}
	}

	void depth_stamp_rect(const Image_buffer8* win, int x, int y, int w, int h) {
		if (!stamping(win) || !clip_rect(win, x, y, w, h)) {
			return;
		}
		for (int r = 0; r < h; r++) {
			uint8_t* row = &depth[static_cast<size_t>(y + r) * dw + x];
			if (in_world) {
				std::memset(row, std::clamp(cur_lift * 4, 0, 254), w);
			} else {
				std::memset(row, DEPTH_UI, w);
			}
		}
	}

	void depth_copy(const Image_buffer8* win, int sx, int sy, int w, int h, int dx, int dy) {
		if (!stamping(win)) {
			return;
		}
		// Same clipping against the buffer as the pixel copy (scrolling).
		if (sx < 0) { w += sx; dx -= sx; sx = 0; }
		if (dx < 0) { w += dx; sx -= dx; dx = 0; }
		if (sy < 0) { h += sy; dy -= sy; sy = 0; }
		if (dy < 0) { h += dy; sy -= dy; dy = 0; }
		w = std::min({w, dw - sx, dw - dx});
		h = std::min({h, dh - sy, dh - dy});
		if (w <= 0 || h <= 0) {
			return;
		}
		if (dy <= sy) {
			for (int r = 0; r < h; r++) {
				std::memmove(&depth[static_cast<size_t>(dy + r) * dw + dx], &depth[static_cast<size_t>(sy + r) * dw + sx], w);
			}
		} else {
			for (int r = h - 1; r >= 0; r--) {
				std::memmove(&depth[static_cast<size_t>(dy + r) * dw + dx], &depth[static_cast<size_t>(sy + r) * dw + sx], w);
			}
		}
	}

	void stereo_tick() {
		Game_window* gw     = Game_window::get_instance();
		const bool   ingame = gw && gw->get_main_actor() != nullptr;
		const bool   want   = ingame && n3ds_game_on_top() && osGet3DSliderState() > 0.0f;
		if (want && !depth_active) {
			depth_active = ensure_buffer();
			need_repaint = depth_active;
			printf("[exult-3ds] 3D on: depth %dx%d\n", dw, dh);
		} else if (!want && depth_active) {
			depth_active = false;
		}
		if (need_repaint && gw) {
			need_repaint = false;
			gw->set_all_dirty();    // rebuild the whole height map
		}
	}
}    // namespace n3ds

/*
 * Called by SDL's 3DS backend after the left eye has been copied, while the
 * slider is up. src is the window surface (XRGB8888, game pixels 1:1),
 * rightfb the right-eye framebuffer (rotated: fbw = 240 rows of 400).
 * Returns 0 to have SDL show the left image to both eyes instead.
 */
extern "C" int N3DS_StereoFrame(
		const void* src, int sw, int sh, int pitch, int bpp, void* rightfb, int fbw, int fbh, float slider) {
	// The game area (dw x dh) sits centred in the window surface (sw x sh).
	if (!n3ds::depth_active || bpp != 4 || sw < dw || sh != dh || depth.empty() || fbw < sh || fbh < sw) {
		return 0;
	}
	const u64 t0 = svcGetSystemTick();
	int       disp[256];
	for (int v = 0; v < 255; v++) {
		const float p = slider * (GROUND_PARALLAX - v * PARALLAX_PER_Q);
		disp[v]       = static_cast<int>(p >= 0 ? p + 0.5f : p - 0.5f);
	}
	disp[DEPTH_UI] = 0;

	// Windows, conversations, text and the cursor are composited layers that
	// are not in the height map: put them on the screen plane.
	static std::vector<uint8_t> frame_depth;
	const int                   xoff = (sw - dw) / 2;
	frame_depth.assign(static_cast<size_t>(sw) * sh, DEPTH_UI);    // borders: screen plane
	for (int y = 0; y < dh; y++) {
		std::memcpy(&frame_depth[static_cast<size_t>(y) * sw + xoff], &depth[static_cast<size_t>(y) * dw], dw);
	}
	if (Game_window* gw = Game_window::get_instance(); gw && gw->get_win()) {
		gw->get_win()->n3ds_mark_layers(frame_depth.data(), sw, sh, DEPTH_UI);
	}

	static std::vector<uint32_t> row;
	static std::vector<int16_t>  zb;
	row.resize(sw);
	zb.resize(sw);
	auto* out = static_cast<uint32_t*>(rightfb);
	for (int y = 0; y < sh; y++) {
		const auto*    s = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(src) + static_cast<size_t>(y) * pitch);
		const uint8_t* d = &frame_depth[static_cast<size_t>(y) * sw];
		std::fill(zb.begin(), zb.end(), static_cast<int16_t>(-1));
		// Forward-project each pixel by its parallax; nearer pixels win.
		for (int x = 0; x < sw; x++) {
			const int v  = d[x];
			const int nx = x + disp[v];
			if (nx >= 0 && nx < sw && v >= zb[nx]) {
				zb[nx]  = static_cast<int16_t>(v);
				row[nx] = s[x];
			}
		}
		// Fill the gaps that open up beside raised things from the left.
		uint32_t prev = s[0];
		for (int x = 0; x < sw; x++) {
			if (zb[x] < 0) {
				row[x] = prev;
			}
			prev = row[x];
		}
		uint32_t* o = out + (fbw - y - 1);
		for (int x = 0; x < sw; x++, o += fbw) {
			*o = row[x];
		}
	}
	static u64 acc = 0;
	static int n   = 0;
	acc += svcGetSystemTick() - t0;
	if (++n == 120) {
		printf("[exult-3ds] 3D right eye: %.2f ms/frame\n", acc / 120.0 / (SYSCLOCK_ARM11 / 1000.0));
		acc = 0;
		n   = -100000;    // report once
	}
	return 1;
}
