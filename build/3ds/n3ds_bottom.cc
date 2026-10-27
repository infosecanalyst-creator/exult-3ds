/*
 * Exult for New Nintendo 3DS - two-screen layout.
 *
 * You play on the touchscreen: the game (320x240, pixel for pixel) is shown
 * on the bottom screen and the stylus is the mouse. The top screen is a
 * dashboard you glance at:
 *   - party strip:  portrait, health and mana bars and a status tag per member
 *   - minimap:      terrain around the party, people marked, and a box for
 *                   what the game screen shows
 *   - info column:  day and time, combat state, what the cursor is on,
 *                   problems ("Iolo is hungry") and recent messages
 * A small menu button (three bars) in the game's top-right corner opens a
 * pop-up with the party's inventories and the rest of the commands.
 * L+START swaps the screens: the game goes back to the top screen (where the
 * 3D slider works) and the dashboard moves to the bottom screen, whose
 * touches then point and click 1:1 on the game above. L+Y opens a guide.
 *
 * The dashboard is drawn with Exult's own shapes, fonts and palette into an
 * 8-bit buffer and converted to an RGB565 framebuffer a few times a second.
 * The game itself is copied to its screen by SDL's 3DS backend
 * (N3DS_GameScreen); touches arrive via N3DS_BottomTouch.
 *
 * GPL v2 (same as Exult).
 */
#include <3ds.h>

#include "Gump.h"
#include "Gump_manager.h"
#include "actors.h"
#include "chunkter.h"
#include "combat_opts.h"
#include "conversation.h"
#include "exult_constants.h"
#include "game.h"
#include "font.h"
#include "gameclk.h"
#include "gamemap.h"
#include "gamewin.h"
#include "ibuf8.h"
#include "mouse.h"
#include "n3ds_stereo.h"
#include "palette.h"
#include "party.h"
#include "schedule.h"
#include "shapeid.h"
#include "shapeinf.h"
#include "shapeinf/npcdollinf.h"
#include "ucmachine.h"
#include "vgafile.h"
#include "manual_text.h"

#ifdef __GNUC__
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <SDL3/SDL.h>
#ifdef __GNUC__
#	pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef N3DS_AUTOSAVE_MS
#	define N3DS_AUTOSAVE_MS (15 * 60 * 1000)
#endif

extern "C" void N3DS_SetCursor(float x, float y);
extern "C" void N3DS_Represent(void);
extern "C" void n3ds_log_flush();
extern "C" void n3ds_show_legend();
extern "C" unsigned long n3ds_cache_report(unsigned long* cached_kb);
extern "C" int n3ds_heap_used_mb();
extern "C" void n3ds_io_report(char* out, unsigned n);
extern "C" bool n3ds_write_report(char* out, unsigned n);
extern "C" void n3ds_sd_report(char* out, unsigned n);

namespace {
	// The SDL window is the top screen's 400x240; Exult draws its 320x240
	// game area in the middle of it.
	constexpr int WIN_W = 400, GAME_W = 320, H = 240, XOFF = (WIN_W - GAME_W) / 2;
	constexpr int MAX_PW = 400;

	constexpr int SB_COMBAT = 0, SB_DISK = 1, SB_KEYRING = 3, SB_SPELLBOOK = 4, SB_TARGET = 5, SB_FOOD = 6, SB_MAPS = 8,
				  SB_NOTEBOOK = 9;

	struct Hit {
		int         x, y, w, h;
		SDL_Keycode key;
		SDL_Keymod  mod;
		int         id;
	};

	// Layout state
	bool game_on_top  = false;    // L+START: game back on the top screen
	bool in_game      = false;
	bool pending_swap = false;
	int  PW           = MAX_PW;    // dashboard width: 400 on top, 320 on bottom

	Image_buffer8*   buf  = nullptr;    // dashboard
	Image_buffer8*   mbuf = nullptr;    // menu pop-up (320x240)
	uint16_t         rgb[256];
	uint32_t         rgb32[256];
	bool             pal_loaded     = false;
	bool             day_pal_cached = false;
	uint64_t         last_draw      = 0;
	bool             menu_open      = false;
	bool             menu_dirty     = true;
	bool             overlay_dirty  = false;
	std::vector<Hit> mhits;    // menu buttons (bottom-screen coordinates)
	int              pressed_id  = -1;
	int              touch_hit   = -1;
	bool             touch_ui    = false;
	int              help_page   = -1;    // guide page, -1 = closed
	bool             any_tag_now = false;
	unsigned char    C_BG, C_PANEL, C_PANEL_HI, C_EDGE, C_RED, C_BLUE, C_GREEN, C_DIM, C_TEXT, C_YELLOW;

	// Menu button: top-right corner of the bottom screen.
	constexpr int MB_X = GAME_W - 30, MB_Y = 2, MB_W = 28, MB_H = 22;
	constexpr int ID_MENU_BUTTON = 399;

	// Zoom (ZL): the touchscreen shows a smaller part of the game, enlarged.
	constexpr int ZOOM_STEPS = 3;
	const int     zoom_w[ZOOM_STEPS] = {320, 214, 160}, zoom_h[ZOOM_STEPS] = {240, 160, 120};
	const char*   zoom_name[ZOOM_STEPS] = {"", "ZOOM 1.5x", "ZOOM 2x"};
	int           zoom = 0;
	int           view_x = 0, view_y = 0, view_w = 320, view_h = 240;    // game coords on the touchscreen
	uint64_t      last_present = 0;    // last time the game drew a frame on the touchscreen

	// World map on the top screen (Black Gate): M / D-Pad up / menu Map.
	bool     map_shown = false;
	bool     pending_map = false;    // toggle requested (from input; done in the tick)

	// Party stats table on the top screen (D-Pad down / menu Stats).
	bool stats_shown   = false;
	bool pending_stats = false;

	// Books, scrolls and signs: read on the top screen.
	const void*              read_who  = nullptr;
	std::string              read_text;
	bool                     read_more = false;
	bool                     read_sign = false;
	int                      sign_shape = 0, sign_font = 1;
	bool                     sign_serp  = false;
	std::vector<std::string> sign_lines;

	// Conversations: the speaker's portrait and words on the top screen,
	// the answers as big buttons on the touchscreen.
	bool        conv_has = false;
	int         conv_face_shape = -1, conv_face_frame = 0, conv_face_file = 0;
	std::string conv_text;
	bool        conv_choices = false;          // answers waiting (updated by the tick)
	int         answer_page  = 0;
	std::string conv_sig;                      // what the answer panel shows
	int         cb_y0        = H;              // top of the bottom-screen panel (H = none)
	int         cb_mode      = 0;              // 0 none, 1 answers, 2 "tap to continue" bar
	struct CHit {
		int x, y, w, h, answer;    // answer index, -1 = "More..."
	};
	std::vector<CHit> chits;
	int               cpressed = -2;
	std::string map_msg;
	uint64_t map_msg_until = 0;

	// Recent messages (what people say, what happens).
	std::deque<std::string> messages;

	unsigned char nearest(int r, int g, int b) {
		int best = 0, bestd = 1 << 30;
		for (int i = 0; i < 256; i++) {
			const int cr = (rgb32[i] >> 24) & 255, cg = (rgb32[i] >> 16) & 255, cb = (rgb32[i] >> 8) & 255;
			const int d  = (cr - r) * (cr - r) + (cg - g) * (cg - g) + (cb - b) * (cb - b);
			if (d < bestd) {
				bestd = d;
				best  = i;
			}
		}
		return static_cast<unsigned char>(best);
	}

	void load_palette(Game_window* gwin) {
		Palette*  pal = gwin->get_pal();
		const int mx  = pal->get_max_val() > 0 ? pal->get_max_val() : 63;
		for (int i = 0; i < 256; i++) {
			const int r = pal->get_red(i) * 255 / mx, g = pal->get_green(i) * 255 / mx, b = pal->get_blue(i) * 255 / mx;
			rgb[i]      = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
			rgb32[i] = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | 0xFF;
		}
		C_BG       = nearest(18, 14, 10);
		C_PANEL    = nearest(70, 50, 30);
		C_PANEL_HI = nearest(150, 110, 50);
		C_EDGE     = nearest(120, 90, 50);
		C_RED      = 22;
		C_GREEN    = 64;
		C_BLUE     = 79;
		C_DIM      = nearest(40, 32, 24);
		C_TEXT     = nearest(230, 220, 190);
		C_YELLOW   = nearest(240, 220, 60);
		pal_loaded = true;
	}

	// ---- Screens --------------------------------------------------------------
	// Game on the bottom: bottom = 32-bit double-buffered (SDL), top = RGB565
	// single-buffered (dashboard). Swapped: the other way round.
	void apply_layout() {
		const gfxScreen_t game  = game_on_top ? GFX_TOP : GFX_BOTTOM;
		const gfxScreen_t panel = game_on_top ? GFX_BOTTOM : GFX_TOP;
		const int         pw    = panel == GFX_TOP ? 400 : 320;
		gfxSetScreenFormat(game, GSP_RGBA8_OES);
		gfxSetDoubleBuffering(game, true);
		gfxSetScreenFormat(panel, GSP_RGB565_OES);
		gfxSetDoubleBuffering(panel, false);
		auto* fb = reinterpret_cast<uint16_t*>(gfxGetFramebuffer(panel, GFX_LEFT, nullptr, nullptr));
		std::memset(fb, 0, pw * 240 * 2);
		GSPGPU_FlushDataCache(fb, pw * 240 * 2);
		gfxScreenSwapBuffers(panel, false);
		PW         = pw;
		last_draw  = 0;
		menu_dirty = true;
		N3DS_Represent();
	}

	bool layout_ok() {
		const gfxScreen_t game  = game_on_top ? GFX_TOP : GFX_BOTTOM;
		const gfxScreen_t panel = game_on_top ? GFX_BOTTOM : GFX_TOP;
		return gfxGetScreenFormat(game) == GSP_RGBA8_OES && gfxGetScreenFormat(panel) == GSP_RGB565_OES;
	}

	void blit_panel() {
		const gfxScreen_t panel = game_on_top ? GFX_BOTTOM : GFX_TOP;
		auto*             fb    = reinterpret_cast<uint16_t*>(gfxGetFramebuffer(panel, GFX_LEFT, nullptr, nullptr));
		// In the swapped layout an open menu covers the dashboard.
		Image_buffer8*       src = (game_on_top && menu_open && mbuf) ? mbuf : buf;
		const unsigned char* s   = src->get_bits();
		const int            lw  = src->get_line_width();
		for (int y = 0; y < H; y++) {
			const unsigned char* row = s + y * lw;
			for (int x = 0; x < PW; x++) {
				fb[x * 240 + (239 - y)] = rgb[row[x]];
			}
		}
		GSPGPU_FlushDataCache(fb, PW * H * 2);
	}

	void push_key_fwd(SDL_Keycode key);

	// ---- Drawing helpers ------------------------------------------------------
	Image_buffer8* target = nullptr;    // buffer the helpers draw into

	Shape_frame* sb_icon(int shape) {
		ShapeID sid(shape, 0, SF_SHORTCUTBAR_VGA);
		return sid.get_shape();
	}

	void paint_centered(Shape_frame* f, int cx, int cy) {
		if (f) {
			f->paint_rle(target, cx - f->get_width() / 2 + f->get_xleft(), cy - f->get_height() / 2 + f->get_yabove());
		}
	}

	std::shared_ptr<Font> font(int n) {
		return Shape_manager::get_instance()->get_font(n);
	}

	void text(int fontnum, const char* s, int x, int y) {
		if (auto f = font(fontnum)) {
			f->paint_text(target, s, x, y);
		}
	}

	void text_fit(int fontnum, std::string s, int x, int y, int maxw) {
		auto f = font(fontnum);
		if (!f) {
			return;
		}
		while (s.size() > 1 && f->get_text_width(s.c_str()) > maxw) {
			s.pop_back();
		}
		f->paint_text(target, s.c_str(), x, y);
	}

	void text_centered(int fontnum, const char* s, int cx, int y, int maxw = MAX_PW) {
		auto f = font(fontnum);
		if (!f) {
			return;
		}
		std::string str(s);
		while (!str.empty() && f->get_text_width(str.c_str()) > maxw) {
			str.pop_back();
		}
		f->paint_text(target, str.c_str(), cx - f->get_text_width(str.c_str()) / 2, y);
	}

	// Word-wrap into lines no wider than maxw.
	std::vector<std::string> wrap(int fontnum, const std::string& s, int maxw) {
		std::vector<std::string> out;
		auto                     f = font(fontnum);
		if (!f) {
			return out;
		}
		std::string line, word;
		for (size_t i = 0; i <= s.size(); i++) {
			const char c = i < s.size() ? s[i] : ' ';
			if (c != ' ') {
				word += c;
				continue;
			}
			if (word.empty()) {
				continue;
			}
			const std::string cand = line.empty() ? word : line + " " + word;
			if (f->get_text_width(cand.c_str()) <= maxw) {
				line = cand;
			} else {
				if (!line.empty()) {
					out.push_back(line);
				}
				line = word;
				while (line.size() > 1 && f->get_text_width(line.c_str()) > maxw) {
					line.pop_back();
				}
			}
			word.clear();
		}
		if (!line.empty()) {
			out.push_back(line);
		}
		return out;
	}

	void panel(int x, int y, int w, int h, bool hi) {
		target->fill8(C_EDGE, w, h, x, y);
		target->fill8(hi ? C_PANEL_HI : C_PANEL, w - 2, h - 2, x + 1, y + 1);
	}

	void bar(int x, int y, int w, int val, int maxv, unsigned char col) {
		target->fill8(C_DIM, w, 3, x, y);
		int f = maxv > 0 ? val * w / maxv : 0;
		if (f > w) {
			f = w;
		}
		if (f > 0) {
			target->fill8(col, f, 3, x, y);
		}
	}

	Shape_frame* face_of(Actor* a) {
		const Paperdoll_npc* npcinfo = a->get_info().get_npc_paperdoll();
		if (!npcinfo) {
			npcinfo = ShapeID::get_info(a->get_sexed_coloured_shape()).get_npc_paperdoll();
		}
		if (!npcinfo) {
			npcinfo = ShapeID::get_info(a->get_shape_real()).get_npc_paperdoll_safe(a->get_type_flag(Actor::tf_sex));
		}
		if (!npcinfo) {
			return nullptr;
		}
		ShapeID sid(npcinfo->get_head_shape(), npcinfo->get_head_frame(), SF_PAPERDOL_VGA);
		return sid.get_shape();
	}

	std::vector<Actor*> party_members(Game_window* gwin) {
		std::vector<Actor*> members;
		if (Actor* av = gwin->get_main_actor()) {
			members.push_back(av);
		}
		Party_manager* pm = gwin->get_party_man();
		for (int i = 0; pm && i < pm->get_count() && members.size() < 8; i++) {
			if (Actor* a = gwin->get_npc(pm->get_member(i))) {
				members.push_back(a);
			}
		}
		return members;
	}

	void draw_face(Actor* a, int x, int y, int w, int h) {
		target->set_clip(x, y, w, h);
		if (Shape_frame* f = face_of(a)) {
			paint_centered(f, x + w / 2, y + h / 2 + 2);
		} else {
			const std::string n = a->get_name();
			text_centered(0, n.substr(0, 1).c_str(), x + w / 2, y + h / 2 - 5);
		}
		target->clear_clip();
	}

	// ---- Dashboard: party strip --------------------------------------------
	constexpr int PARTY_H = 48;

	void draw_party(Game_window* gwin) {
		target->fill8(C_BG, PW, PARTY_H, 0, 0);
		const std::vector<Actor*> members = party_members(gwin);
		const int                 slot    = PW / 8;
		any_tag_now                       = false;
		for (size_t i = 0; i < members.size(); i++) {
			Actor*     a    = members[i];
			const int  sx   = static_cast<int>(i) * slot;
			const bool hurt = a->get_flag(Obj_flags::poisoned) || a->was_hit();
			panel(sx + 1, 1, slot - 2, PARTY_H - 2, false);
			if (hurt) {
				target->fill8(a->get_flag(Obj_flags::poisoned) ? C_GREEN : C_RED, slot - 4, 2, sx + 2, 2);
			}
			draw_face(a, sx + 2, 2, slot - 4, 32);
			bar(sx + 4, 36, slot - 8, a->get_effective_prop(Actor::health), a->get_effective_prop(Actor::strength), C_RED);
			if (a->get_effective_prop(Actor::magic) > 0) {
				bar(sx + 4, 41, slot - 8, a->get_effective_prop(Actor::mana), a->get_effective_prop(Actor::magic), C_BLUE);
			}
			const char*   tag  = nullptr;
			unsigned char tc   = C_TEXT;
			const int     food = a->get_property(static_cast<int>(Actor::food_level));
			const bool    eats = !a->get_info().does_not_eat();
			if (a->get_flag(Obj_flags::paralyzed)) {
				tag = "PAR"; tc = C_RED;
			} else if (a->get_flag(Obj_flags::asleep)) {
				tag = "ZZ"; tc = C_BLUE;
			} else if (a->get_flag(Obj_flags::poisoned)) {
				tag = "PSN"; tc = C_GREEN;
			} else if (a->get_flag(Obj_flags::charmed)) {
				tag = "CHM"; tc = C_RED;
			} else if (eats && food <= 9) {
				tag = food <= 0 ? "STRV" : "FOOD"; tc = C_RED;
			}
			if (tag) {
				any_tag_now = true;
				const int tw = font(0) ? font(0)->get_text_width(tag) : 22;
				target->fill8(tc, std::min(tw + 4, slot - 4), 13, sx + 2, 21);
				text(0, tag, sx + 4, 22);
			}
		}
	}

	// In the swapped layout the dashboard is on the touchscreen: show the
	// menu button in the same corner as over the game.
	void draw_menu_button() {
		const bool hi = pressed_id == ID_MENU_BUTTON;
		target->fill8(hi ? C_TEXT : C_EDGE, MB_W, MB_H, MB_X, MB_Y);
		target->fill8(C_BG, MB_W - 2, MB_H - 2, MB_X + 1, MB_Y + 1);
		for (int k = 0; k < 3; k++) {
			target->fill8(C_TEXT, MB_W - 14, 2, MB_X + 7, MB_Y + 6 + k * 4);
		}
	}

	// ---- Dashboard: minimap -------------------------------------------------
	// Terrain colours (one per tile, sampled from the middle of each 8x8 tile
	// of the chunk's rendered ground) at 2x2 pixels per tile, centred on the
	// Avatar, with people marked and the game screen's view outlined.
	constexpr int MM_SCALE = 2, MM_Y = PARTY_H + 2, MM_H = H - MM_Y;
	constexpr int INFO_W   = 176;    // room for the readable font

	std::unordered_map<int, std::vector<unsigned char>> terrain_thumbs;
	std::vector<unsigned char>                          mm_tiles;
	int                                                 mm_ox = 0, mm_oy = 0, mm_map = -1, mm_tw = 0;
	uint64_t                                            mm_built = 0;

	int mm_w() {
		return PW - INFO_W - 4;
	}

	const std::vector<unsigned char>& terrain_thumb(int tnum) {
		auto it = terrain_thumbs.find(tnum);
		if (it != terrain_thumbs.end()) {
			return it->second;
		}
		if (terrain_thumbs.size() > 1500) {
			terrain_thumbs.clear();    // keep memory bounded on long journeys
		}
		std::vector<unsigned char> t(c_tiles_per_chunk * c_tiles_per_chunk, C_DIM);
		if (Chunk_terrain* ter = Game_map::get_terrain(tnum)) {
			if (Image_buffer8* flats = ter->get_rendered_flats()) {
				const unsigned char* bits = flats->get_bits();
				const int            lw   = flats->get_line_width();
				for (int ty = 0; ty < c_tiles_per_chunk; ty++) {
					for (int tx = 0; tx < c_tiles_per_chunk; tx++) {
						t[ty * c_tiles_per_chunk + tx]
								= bits[(ty * c_tilesize + c_tilesize / 2) * lw + tx * c_tilesize + c_tilesize / 2];
					}
				}
			}
		}
		return terrain_thumbs.emplace(tnum, std::move(t)).first->second;
	}

	void draw_minimap(Game_window* gwin) {
		const int        W      = mm_w();
		const int        TW     = W / MM_SCALE, TH = MM_H / MM_SCALE;
		Actor*           av     = gwin->get_main_actor();
		const Tile_coord at     = av->get_tile();
		const int        ox     = at.tx - TW / 2;
		const int        oy     = at.ty - TH / 2;
		const uint64_t   now    = SDL_GetTicks();
		const int        mapnum = gwin->get_map()->get_num();
		if (mapnum != mm_map || ox != mm_ox || oy != mm_oy || TW != mm_tw || now - mm_built > 3000) {
			mm_tiles.resize(static_cast<size_t>(TW) * TH);
			Game_map* gmap = gwin->get_map();
			for (int y = 0; y < TH; y++) {
				const int ty = (oy + y + c_num_tiles) % c_num_tiles;
				for (int x = 0; x < TW; x++) {
					const int   tx   = (ox + x + c_num_tiles) % c_num_tiles;
					const int   tnum = gmap->get_terrain_num(tx / c_tiles_per_chunk, ty / c_tiles_per_chunk);
					const auto& th   = terrain_thumb(tnum);
					mm_tiles[y * TW + x] = th[(ty % c_tiles_per_chunk) * c_tiles_per_chunk + tx % c_tiles_per_chunk];
				}
			}
			mm_ox    = ox;
			mm_oy    = oy;
			mm_map   = mapnum;
			mm_tw    = TW;
			mm_built = now;
		}
		const int      x0   = 0, y0 = MM_Y;
		unsigned char* bits = target->get_bits();
		const int      lw   = target->get_line_width();
		for (int y = 0; y < TH; y++) {
			for (int x = 0; x < TW; x++) {
				const unsigned char c = mm_tiles[y * TW + x];
				unsigned char*      p = bits + (y0 + y * MM_SCALE) * lw + x0 + x * MM_SCALE;
				p[0] = p[1] = p[lw] = p[lw + 1] = c;
			}
		}
		auto to_px = [&](int tx, int ty, int& px, int& py) {
			px = x0 + Tile_coord::delta(ox, tx) * MM_SCALE;
			py = y0 + Tile_coord::delta(oy, ty) * MM_SCALE;
			return px >= x0 + 1 && py >= y0 + 1 && px < x0 + W - 3 && py < y0 + MM_H - 3;
		};
		{    // what the game screen shows
			int px, py;
			to_px(gwin->get_scrolltx(), gwin->get_scrollty(), px, py);
			const int vw = gwin->get_width() / c_tilesize * MM_SCALE;
			const int vh = gwin->get_height() / c_tilesize * MM_SCALE;
			target->set_clip(x0, y0, W, MM_H);
			target->fill8(C_TEXT, vw, 1, px, py);
			target->fill8(C_TEXT, vw, 1, px, py + vh - 1);
			target->fill8(C_TEXT, 1, vh, px, py);
			target->fill8(C_TEXT, 1, vh, px + vw - 1, py);
			target->clear_clip();
		}
		auto dot = [&](int x, int y, unsigned char col, int size) {
			target->fill8(C_BG, size + 2, size + 2, x - 1, y - 1);
			target->fill8(col, size, size, x, y);
		};
		std::vector<Actor*> npcs;
		gwin->get_nearby_npcs(npcs);
		for (Actor* a : npcs) {
			if (!a || a == av || a->is_dead() || a->get_flag(Obj_flags::invisible)) {
				continue;
			}
			int              px, py;
			const Tile_coord t = a->get_tile();
			if (to_px(t.tx, t.ty, px, py)) {
				const int  al    = a->get_effective_alignment();
				const bool party = a->get_flag(Obj_flags::in_party);
				const bool foe   = al == Actor::evil || al == Actor::chaotic;
				dot(px, py, party ? C_GREEN : foe ? C_RED : C_YELLOW, 2);
			}
		}
		int ax, ay;
		if (to_px(at.tx, at.ty, ax, ay)) {
			dot(ax - 1, ay - 1, C_TEXT, 4);
		}
	}

	// ---- Dashboard: info column ---------------------------------------------
	void draw_info(Game_window* gwin) {
		const int x0 = mm_w() + 2, w = PW - x0;
		target->fill8(C_BG, w, H - MM_Y, x0, MM_Y);
		target->fill8(C_EDGE, 1, H - MM_Y, x0, MM_Y);
		const int sx = x0 + 5, sw = w - 8;
		int       y  = MM_Y + 3;
		if (Game_clock* clk = gwin->get_clock()) {
			char      tbuf[48];
			const int h = clk->get_hour();
			std::snprintf(tbuf, sizeof tbuf, "Day %d  %d:%02d %s", clk->get_day() + 1, h % 12 == 0 ? 12 : h % 12,
						  clk->get_minute(), h < 12 ? "AM" : "PM");
			text_fit(0, tbuf, sx, y, sw);
		}
		y += 15;
		if (!map_msg.empty() && SDL_GetTicks() < map_msg_until) {
			text_fit(0, map_msg, sx, y, sw);
			y += 15;
		}
		if (zoom > 0) {
			target->fill8(C_BLUE, sw, 14, sx - 1, y);
			text(0, zoom_name[zoom], sx + 2, y + 1);
			y += 17;
		}
		if (gwin->in_combat()) {
			target->fill8(C_RED, sw, 14, sx - 1, y);
			text(0, Combat::is_paused() ? "COMBAT - PAUSED" : "COMBAT", sx + 2, y + 1);
			y += 17;
		}
		// What the cursor is on.
		{
			Gump_manager* gm  = gwin->get_gump_man();
			Mouse*        m   = Mouse::mouse();
			Game_object*  obj = nullptr;
			if (m && gm) {
				const int mx = m->get_mousex(), my = m->get_mousey();
				if (Gump* g = gm->find_gump(mx, my)) {
					int gx, gy;
					gm->map_game_to_gump(g, mx, my, gx, gy);
					obj = g->find_object(gx, gy);
				} else {
					obj = gwin->find_object(mx, my);
				}
			}
			if (obj) {
				const auto lines = wrap(0, obj->get_name(), sw);
				for (size_t k = 0; k < lines.size() && k < 2; k++, y += 13) {
					text(0, lines[k].c_str(), sx, y);
				}
				if (Actor* a = obj->as_actor()) {
					if (!a->is_dead()) {
						bar(sx, y + 1, sw, a->get_effective_prop(Actor::health), a->get_effective_prop(Actor::strength), C_RED);
						y += 6;
					}
				} else if (obj->get_quantity() > 1) {
					char qbuf[16];
					std::snprintf(qbuf, sizeof qbuf, "x%d", obj->get_quantity());
					text(0, qbuf, sx, y);
					y += 13;
				}
				y += 3;
			}
		}
		// Problems.
		{
			std::vector<std::string> alerts;
			for (Actor* a : party_members(gwin)) {
				const std::string n    = a->get_name();
				const int         food = a->get_property(static_cast<int>(Actor::food_level));
				const int hp = a->get_effective_prop(Actor::health), maxhp = a->get_effective_prop(Actor::strength);
				if (a->get_flag(Obj_flags::poisoned)) {
					alerts.push_back(n + " is poisoned");
				}
				if (a->get_flag(Obj_flags::paralyzed)) {
					alerts.push_back(n + " is paralyzed");
				}
				if (!a->get_info().does_not_eat() && food <= 9) {
					alerts.push_back(n + (food <= 0 ? " is starving" : " is hungry"));
				}
				if (maxhp > 0 && hp * 4 <= maxhp) {
					alerts.push_back(n + " is badly hurt");
				}
			}
			if (alerts.empty()) {
				text(0, "Party OK", sx, y);
				y += 13;
			}
			for (size_t k = 0; k < alerts.size() && k < 3; k++, y += 13) {
				target->fill8(C_RED, 3, 9, sx - 1, y + 1);
				text_fit(0, alerts[k], sx + 4, y, sw - 4);
			}
		}
		// Recent messages, newest at the bottom.
		y += 3;
		target->fill8(C_EDGE, sw, 1, sx, y);
		y += 3;
		const int line_h = 13, bottom = H - 1;
		const int room   = (bottom - y) / line_h;
		if (room <= 0) {
			return;
		}
		std::vector<std::string> lines;    // newest first
		for (auto it = messages.rbegin(); it != messages.rend() && static_cast<int>(lines.size()) < room; ++it) {
			auto w = wrap(0, *it, sw);
			for (auto l = w.rbegin(); l != w.rend(); ++l) {
				lines.push_back(*l);
			}
		}
		if (lines.empty()) {
			text(0, "(recent messages)", sx, y);
			return;
		}
		const int n  = std::min(room, static_cast<int>(lines.size()));
		int       ly = bottom - n * line_h;
		for (int k = n - 1; k >= 0; k--, ly += line_h) {
			text(0, lines[k].c_str(), sx, ly);
		}
	}

	// ---- World map -------------------------------------------------------------
	int party_count_of(Game_window* gwin, int shapenum) {
		int n = 0;
		for (Actor* a : party_members(gwin)) {
			n += a->count_objects(shapenum);
		}
		return n;
	}

	void draw_world_map(Game_window* gwin) {
		target->fill8(C_BG, PW, H, 0, 0);
		ShapeID      msid(game->get_shape("sprites/map"), 0, SF_SPRITES_VGA);
		Shape_frame* s = msid.get_shape();
		if (!s) {
			map_shown = false;
			return;
		}
		const int x0 = (PW - s->get_width()) / 2, y0 = std::max(0, (H - 14 - s->get_height()) / 2);
		s->paint_rle(target, x0 + s->get_xleft(), y0 + s->get_yabove());
		// "You are here", as the game shows it: only with a sextant, outdoors.
		const bool loc = !gwin->is_main_actor_inside() && party_count_of(gwin, 650) > 0;
		if (loc) {
			const Tile_coord t  = gwin->get_main_actor()->get_tile();
			const int        xx = x0 + static_cast<int>(t.tx / 16.05 + 5.5);
			const int        yy = y0 + static_cast<int>(t.ty / 15.95 + 4.5);
			const bool       on = (SDL_GetTicks() / 400) % 2 == 0;
			target->fill8(on ? C_RED : C_TEXT, 2, 9, xx, yy - 4);
			target->fill8(on ? C_RED : C_TEXT, 9, 2, xx - 4, yy - 1 + 1);
		}
		target->fill8(C_BG, PW, 14, 0, H - 14);
		text_centered(0, loc ? "You are here (blinking).  D-Pad up or B: close" : "Need a sextant to see where you are.  B: close",
					  PW / 2, H - 13, PW - 8);
	}

	// Open/close the map on the top screen (BG); Serpent Isle keeps its own.
	void toggle_world_map(Game_window* gwin) {
		if (map_shown) {
			map_shown = false;
		} else if (Game::get_game_type() != BLACK_GATE) {
			push_key_fwd(SDLK_M);
			return;
		} else if (party_count_of(gwin, 178) <= 0) {
			map_msg       = "The party has no map.";
			map_msg_until = SDL_GetTicks() + 3000;
		} else {
			map_shown = true;
			help_page = -1;
		}
		last_draw = 0;
	}

	// ---- Reading on the top screen ----------------------------------------------
	// Paragraphs: '~' is a line break, '*' a page break.
	std::vector<std::string> book_lines(const std::string& t, int maxw) {
		std::vector<std::string> out;
		std::string              para;
		auto                     flush = [&](bool blank) {
			for (auto& l : wrap(0, para, maxw)) {
				out.push_back(l);
			}
			para.clear();
			if (blank) {
				out.emplace_back();
			}
		};
		for (char c : t) {
			if (c == '~') {
				flush(false);
			} else if (c == '*') {
				flush(true);
			} else {
				para += c;
			}
		}
		flush(false);
		while (!out.empty() && out.back().empty()) {
			out.pop_back();
		}
		return out;
	}

	std::string rune_english(const std::string& s) {
		std::string o;
		for (char ch : s) {
			switch (ch) {
			case '(': o += "TH"; break;
			case ')': o += "EE"; break;
			case '*': o += "NG"; break;
			case '+': o += "EA"; break;
			case ',': o += "ST"; break;
			case '|': o += ' '; break;
			default: o += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			}
		}
		return o;
	}

	void draw_footer(const char* msg) {
		target->fill8(C_PANEL, PW, 16, 0, H - 16);
		text_centered(0, msg, PW / 2, H - 14, PW - 8);
	}

	void draw_reading() {
		target->fill8(C_BG, PW, H, 0, 0);
		if (read_sign) {
			ShapeID      sid(sign_shape, 0, SF_GUMPS_VGA);
			Shape_frame* f  = sid.get_shape();
			int          y1 = 6;
			if (f) {
				const int fx = (PW - f->get_width()) / 2, fy = 4;
				f->paint_rle(target, fx + f->get_xleft(), fy + f->get_yabove());
				// The sign's own lettering, centred on it.
				auto      fnt = font(sign_font);
				const int lh  = fnt ? fnt->get_text_height() : 10;
				const int n   = static_cast<int>(sign_lines.size());
				int       ly  = fy + (f->get_height() - n * (lh + 4)) / 2;
				for (const auto& l : sign_lines) {
					if (fnt && !l.empty()) {
						fnt->paint_text(target, l.c_str(), (PW - fnt->get_text_width(l.c_str())) / 2, ly);
					}
					ly += lh + 4;
				}
				y1 = fy + f->get_height() + 6;
			}
			if (!sign_serp) {    // Britannian runes: the same in plain letters
				for (const auto& l : sign_lines) {
					if (l.empty()) {
						continue;
					}
					if (y1 > H - 32) {
						break;
					}
					text_centered(0, rune_english(l).c_str(), PW / 2, y1, PW - 8);
					y1 += 14;
				}
			}
			draw_footer("Tap the touchscreen to close");
			return;
		}
		const int  maxw  = PW - 24;
		const auto lines = book_lines(read_text, maxw);
		const int  room  = (H - 16 - 8) / 14;
		const int  lh    = static_cast<int>(lines.size()) > room ? 13 : 14;
		int        y     = 6;
		for (const auto& l : lines) {
			if (y + lh > H - 18) {
				break;
			}
			text(0, l.c_str(), 12, y);
			y += lh;
		}
		draw_footer(read_more ? "Tap the touchscreen: next page" : "Tap the touchscreen to close");
	}

	// ---- Conversation on the top screen -------------------------------------
	void draw_conversation() {
		target->fill8(C_BG, PW, H, 0, 0);
		int tx = 10;
		if (conv_face_shape >= 0) {
			ShapeID      sid(conv_face_shape, conv_face_frame, static_cast<ShapeFile>(conv_face_file));
			Shape_frame* f = sid.get_shape();
			if (f) {
				// Portrait at 2x when it fits (faces are about 60 pixels).
				const int w = f->get_width(), h = f->get_height();
				const int sc = (w * 2 <= PW / 3 && h * 2 <= H - 26) ? 2 : 1;
				Image_buffer8 tmp(w, h);
				tmp.fill8(255);
				Image_buffer8* prev = Shape_frame::get_render_target();
				Shape_frame::set_to_render(&tmp);
				f->paint_rle(&tmp, f->get_xleft(), f->get_yabove());
				Shape_frame::set_to_render(prev);
				const unsigned char* src = tmp.get_bits();
				const int            slw = tmp.get_line_width();
				unsigned char*       dst = target->get_bits();
				const int            dlw = target->get_line_width();
				for (int yy = 0; yy < h * sc && 6 + yy < H - 18; yy++) {
					for (int xx = 0; xx < w * sc && 6 + xx < PW; xx++) {
						const unsigned char c = src[(yy / sc) * slw + xx / sc];
						if (c != 255) {
							dst[(6 + yy) * dlw + 6 + xx] = c;
						}
					}
				}
				tx = 6 + w * sc + 10;
			}
		}
		const auto lines = wrap(0, conv_text, PW - tx - 8);
		int        y     = 8;
		for (const auto& l : lines) {
			if (y + 14 > H - 18) {
				break;
			}
			text(0, l.c_str(), tx, y);
			y += 14;
		}
		draw_footer(conv_choices ? "Tap an answer on the touchscreen" : "Tap the touchscreen to continue");
	}

	// ---- Party stats on the top screen ---------------------------------------
	void draw_stats(Game_window* gwin) {
		target->fill8(C_BG, PW, H, 0, 0);
		const bool wide = PW >= 400;
		struct Col {
			const char* head;
			int         x;
		};
		const std::vector<Col> cols = wide ? std::vector<Col>{{"Str", 88}, {"Dex", 116}, {"Int", 144}, {"Cbt", 172}, {"Mag", 200},
															  {"Health", 228}, {"Mana", 284}, {"Lv", 330}, {"Exp", 352}}
										   : std::vector<Col>{{"Str", 88}, {"Dex", 114}, {"Int", 140}, {"Health", 168},
															  {"Mana", 226}, {"Lv", 272}, {"Trn", 292}};
		target->fill8(C_PANEL, PW, 18, 0, 0);
		text(0, "Party", 6, 3);
		for (const auto& c : cols) {
			text(0, c.head, c.x, 3);
		}
		int y = 24;
		for (Actor* a : party_members(gwin)) {
			if (y > H - 34) {
				break;
			}
			text_fit(0, a->get_name(), 6, y, 80);
			for (const auto& c : cols) {
				char        b[24];
				const std::string h(c.head);
				if (h == "Str") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::strength));
				} else if (h == "Dex") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::dexterity));
				} else if (h == "Int") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::intelligence));
				} else if (h == "Cbt") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::combat));
				} else if (h == "Mag") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::magic));
				} else if (h == "Health") {
					std::snprintf(b, sizeof b, "%d/%d", a->get_property(Actor::health), a->get_property(Actor::strength));
				} else if (h == "Mana") {
					std::snprintf(b, sizeof b, "%d/%d", a->get_property(Actor::mana), a->get_property(Actor::magic));
				} else if (h == "Lv") {
					std::snprintf(b, sizeof b, "%d", a->get_level());
				} else if (h == "Exp") {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::exp));
				} else {
					std::snprintf(b, sizeof b, "%d", a->get_property(Actor::training));
				}
				text(0, b, c.x, y);
			}
			y += 18;
		}
		draw_footer("D-Pad down or B: close");
	}

	// ---- Answer buttons on the touchscreen ----------------------------------
	Image_buffer8* cbuf = nullptr;

	void draw_answer_panel(Conversation* conv) {
		Image_buffer8* prev_target = target;
		target                     = cbuf;
		chits.clear();
		if (cb_mode == 1) {
			const int n       = conv->get_num_answers();
			const int perpage = n > 8 ? 7 : 8;
			if (answer_page * perpage >= n) {
				answer_page = 0;
			}
			const int first = answer_page * perpage;
			const int shown = std::min(perpage, n - first) + (n > perpage ? 1 : 0);
			const int rows  = (shown + 1) / 2;
			const int bh = 28, bw = GAME_W / 2 - 6;
			cb_y0 = H - (rows * (bh + 2) + 6);
			target->fill8(C_EDGE, GAME_W, H - cb_y0, 0, cb_y0);
			target->fill8(C_BG, GAME_W, H - cb_y0 - 1, 0, cb_y0 + 1);
			for (int k = 0; k < shown; k++) {
				const bool more = n > perpage && k == shown - 1;
				const int  i    = more ? -1 : first + k;
				const int  bx = 4 + (k % 2) * (GAME_W / 2), by = cb_y0 + 4 + (k / 2) * (bh + 2);
				panel(bx, by, bw, bh, cpressed == i);
				text_centered(0, more ? "More..." : conv->get_answer(i), bx + bw / 2, by + 8, bw - 8);
				chits.push_back({bx, by, bw, bh, i});
			}
		} else if (cb_mode == 2) {
			cb_y0 = H - 18;
			target->fill8(C_PANEL, GAME_W, 18, 0, cb_y0);
			text_centered(0, read_who ? "Reading on the top screen - tap to go on" : "Tap to continue", GAME_W / 2,
						  cb_y0 + 3, GAME_W - 8);
		} else {
			cb_y0 = H;
		}
		target = prev_target;
	}

	// ---- First-time tips -------------------------------------------------------
	enum TipId { TIP_WELCOME, TIP_MENU, TIP_TALK, TIP_COMBAT, TIP_STATUS, TIP_GUIDE, TIP_SWAP, NUM_TIPS };

	const char* const tip_text[NUM_TIPS][2] = {
			{"Play on the touchscreen: tap to click,", "double-tap to use or talk, hold to walk."},
			{"The menu button (three bars, top right", "of the game) has spells, map, save..."},
			{"Tap an answer to reply.", "Tap the text to move it on."},
			{"Combat! R turns it on and off.", "Hold L + D-Pad down to pause."},
			{"A tag on a portrait means trouble:", "FOOD = hungry, PSN = poisoned."},
			{"ZL zooms the touchscreen in (1.5x, 2x).", "L+Y opens the manual on this screen."},
			{"3D: L+START puts the game on the top", "screen. L+START again swaps back."},
	};

	unsigned              tips_seen   = 0;
	bool                  tips_loaded = false;
	int                   tip_active  = -1;
	uint64_t              tip_since = 0, tip_last_end = 0, play_since = 0;
	constexpr const char* TIPS_FILE = "sdmc:/3ds/exult/tips2.dat";

	void tips_save() {
		if (FILE* f = std::fopen(TIPS_FILE, "w")) {
			std::fprintf(f, "%u\n", tips_seen);
			std::fclose(f);
		}
	}

	void tips_load() {
		tips_loaded = true;
		if (FILE* f = std::fopen(TIPS_FILE, "r")) {
			if (std::fscanf(f, "%u", &tips_seen) != 1) {
				tips_seen = 0;
			}
			std::fclose(f);
		}
	}

	void tips_update(Game_window* gwin, Conversation* conv, uint64_t now) {
		if (!tips_loaded) {
			tips_load();
		}
		if (play_since == 0) {
			play_since = now;
		}
		if (tip_active >= 0) {
			if (now - tip_since > 12000) {
				tip_active   = -1;
				tip_last_end = now;
			}
			return;
		}
		if (help_page >= 0 || now - play_since < 3000 || (tip_last_end && now - tip_last_end < 8000)) {
			return;
		}
		auto unseen = [&](int t) {
			return !(tips_seen & (1u << t));
		};
		const bool talking = conv && conv->are_choices_active();
		int        pick    = -1;
		if (unseen(TIP_TALK) && talking) {
			pick = TIP_TALK;
		} else if (unseen(TIP_COMBAT) && gwin->in_combat()) {
			pick = TIP_COMBAT;
		} else if (unseen(TIP_WELCOME)) {
			pick = TIP_WELCOME;
		} else if (unseen(TIP_MENU)) {
			pick = TIP_MENU;
		} else if (unseen(TIP_STATUS) && any_tag_now) {
			pick = TIP_STATUS;
		} else if (unseen(TIP_GUIDE) && now - play_since > 60000) {
			pick = TIP_GUIDE;
		} else if (unseen(TIP_SWAP) && now - play_since > 240000) {
			pick = TIP_SWAP;
		}
		if (pick >= 0) {
			tip_active = pick;
			tip_since  = now;
			tips_seen |= 1u << pick;
			tips_save();
		}
	}

	void draw_tip() {
		if (tip_active < 0) {
			return;
		}
		target->fill8(C_TEXT, PW, PARTY_H, 0, 0);
		target->fill8(C_PANEL_HI, PW - 4, PARTY_H - 4, 2, 2);
		text(0, "TIP", 8, 8);
		text_fit(0, tip_text[tip_active][0], 40, 8, PW - 46);
		text_fit(0, tip_text[tip_active][1], 40, 24, PW - 46);
	}

	// ---- Guide (L+Y) -----------------------------------------------------------
	constexpr int GUIDE_PAGES = 6;

	// Two stacked screens; which: 0 none, 1 top screen, 2 bottom screen, 3 menu button.
	void draw_schematic(int x0, int y0, int which) {
		const int tw = 100, th = 60, bw = 80, bh = 60;
		auto      scr = [&](int x, int y, int w, int h, bool hi, const char* label) {
			target->fill8(C_EDGE, w + 2, h + 2, x - 1, y - 1);
			target->fill8(hi ? C_PANEL_HI : C_PANEL, w, h, x, y);
			text_centered(0, label, x + w / 2, y + h / 2 - 4, w - 4);
		};
		scr(x0, y0, tw, th, which == 1, game_on_top ? "Game (3D)" : "Party, map, info");
		scr(x0 + (tw - bw) / 2, y0 + th + 6, bw, bh, which == 2, game_on_top ? "Info + touch" : "Game: touch");
		const int mx = x0 + (tw - bw) / 2 + bw - 14, my = y0 + th + 8;
		target->fill8(which == 3 ? C_TEXT : C_EDGE, 12, 10, mx, my);
		for (int k = 0; k < 3; k++) {
			target->fill8(C_BG, 8, 1, mx + 2, my + 2 + k * 3);
		}
	}

	struct GuidePage {
		const char* title;
		int         area;
		const char* lines[10];
	};

	const GuidePage guide_pages[GUIDE_PAGES] = {
			{"Two screens",
			 0,
			 {"Bottom screen: the game. Play it with",
			  "the stylus, like a mouse.",
			  "Top screen: your party, a minimap, the",
			  "time, what you point at, any problems",
			  "and recent messages.",
			  "L+START swaps the screens (3D only",
			  "works on the top screen).",
			  "D-Pad left/right: pages.  B: close.", nullptr}},
			{"Playing with the stylus",
			 2,
			 {"Tap: click (look, pick up, press).",
			  "Double-tap: use something or talk.",
			  "Hold the stylus still: walk there.",
			  "Drag: move items. Drop them on a",
			  "person or into an open backpack.",
			  "Talking: tap an answer to reply,",
			  "tap the text to move it on.",
			  "ZL: zoom in 1.5x, 2x, then back.", nullptr}},
			{"The menu button",
			 3,
			 {"Three bars, top right of the game.",
			  "Party portraits: tap for inventory.",
			  "Spells, Notes, Map, Stats, Keys,",
			  "Food, Target, Combat, Save,",
			  "Guide, Swap screens, Game menu.",
			  "Tap outside it or press B to close.", nullptr}},
			{"The top screen",
			 1,
			 {"Portraits: red bar health, blue mana.",
			  "Tags: PSN poisoned  FOOD hungry",
			  "STRV starving  ZZ asleep",
			  "PAR paralyzed  CHM charmed",
			  "Minimap: you white, party green,",
			  "people yellow, foes red. The box is",
			  "what the game screen shows.", nullptr}},
			{"Buttons",
			 0,
			 {"Circle Pad walk    C-Stick cursor",
			  "A click/drag   B walk to cursor",
			  "X use/talk     Y inventory",
			  "D-Pad: up map, down stats,",
			  "left/right next person or item",
			  "R combat  ZL zoom  ZR target",
			  "START menu   SELECT quicksave",
			  "L+START swap screens  L+Y guide",
			  "L+D-Pad: watch, pause, lockpick, food",
			  "L+SELECT quick-restore"}},
			{"3D, saving and tips",
			 0,
			 {"3D: L+START puts the game on the top",
			  "screen, then push the 3D slider up.",
			  "SELECT quicksaves; the game also",
			  "autosaves every 15 minutes when idle.",
			  "Journey Onward resumes your game.",
			  "Tips appear the first time you need",
			  "them. Press X on this page to see",
			  "them again.", nullptr}},
	};

	void draw_guide() {
		target->fill8(C_BG, PW, H, 0, 0);
		const GuidePage& pg = guide_pages[help_page];
		target->fill8(C_PANEL, PW, 20, 0, 0);
		text(0, pg.title, 8, 5);
		char pbuf[16];
		std::snprintf(pbuf, sizeof pbuf, "%d / %d", help_page + 1, GUIDE_PAGES);
		text(0, pbuf, PW - 44, 5);
		int tx = 8;
		if (PW >= 400) {
			draw_schematic(10, 34, pg.area);
			tx = 126;
		}
		int y = 28;
		for (const char* l : pg.lines) {
			if (!l) {
				break;
			}
			text_fit(0, l, tx, y, PW - tx - 4);
			y += 14;
		}
		target->fill8(C_PANEL, PW, 16, 0, H - 16);
		text_centered(0, "D-Pad < >  pages      B  close", PW / 2, H - 14);
	}

	// ---- Manual (L+Y, or Manual in the menu) ----------------------------------
	// Text on the top screen; a button per section on the touchscreen. The text
	// is built in (3ds/manual.txt); sdmc:/3ds/exult/manual.txt replaces it.
	struct MSrc {
		int         kind;    // 0 paragraph, 1 heading, 2 list item, 3 table row, 4 blank
		std::string a, b;    // text (list: marker in a, text in b; table: two cells)
	};
	struct MSection {
		std::string       title;
		std::vector<MSrc> src;
	};
	struct MLine {
		int         kind;    // as MSrc; 5 = continuation of a list item / table cell
		std::string a, b;
	};
	std::vector<MSection> manual;
	int                   manual_scroll = 0;
	int                   manual_lw     = -1;    // panel width the layout was made for
	int                   manual_lsec   = -1;
	std::vector<MLine>    manual_lines;

	std::string trim(const std::string& x) {
		size_t a = 0, b = x.size();
		while (a < b && (x[a] == ' ' || x[a] == '\t' || x[a] == '\r')) {
			a++;
		}
		while (b > a && (x[b - 1] == ' ' || x[b - 1] == '\t' || x[b - 1] == '\r')) {
			b--;
		}
		return x.substr(a, b - a);
	}

	void manual_load() {
		if (!manual.empty()) {
			return;
		}
		std::string text;
		if (FILE* f = std::fopen("sdmc:/3ds/exult/manual.txt", "rb")) {
			char   chunk[4096];
			size_t n;
			while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
				text.append(chunk, n);
			}
			std::fclose(f);
		}
		if (text.find("# ") == std::string::npos) {
			text = N3DS_MANUAL_BUILTIN;
		}
		size_t pos = 0;
		while (pos <= text.size()) {
			size_t e = text.find('\n', pos);
			if (e == std::string::npos) {
				e = text.size();
			}
			const std::string line = trim(text.substr(pos, e - pos));
			pos                    = e + 1;
			if (line.rfind("# ", 0) == 0) {
				manual.push_back({trim(line.substr(2)), {}});
				continue;
			}
			if (manual.empty()) {
				manual.push_back({"Manual", {}});
			}
			auto& src = manual.back().src;
			if (line.empty()) {
				if (!src.empty() && src.back().kind != 4) {
					src.push_back({4, "", ""});
				}
			} else if (line.rfind("## ", 0) == 0) {
				src.push_back({1, trim(line.substr(3)), ""});
			} else if (line.rfind("- ", 0) == 0) {
				src.push_back({2, "-", trim(line.substr(2))});
			} else if (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0])) && line[1] == '.') {
				src.push_back({2, line.substr(0, 2), trim(line.substr(2))});
			} else if (line[0] == '|') {
				std::string rest = line.substr(1);
				const size_t bar  = rest.find('|');
				src.push_back({3, trim(rest.substr(0, bar)), bar == std::string::npos ? "" : trim(rest.substr(bar + 1))});
			} else {
				src.push_back({0, line, ""});
			}
		}
	}

	void manual_layout(int sec) {
		if (manual_lw == PW && manual_lsec == sec) {
			return;
		}
		manual_lw   = PW;
		manual_lsec = sec;
		manual_lines.clear();
		const int body_w = PW - 20, col1 = PW >= 400 ? 112 : 90;
		for (const auto& m : manual[sec].src) {
			switch (m.kind) {
			case 0:
				for (auto& l : wrap(0, m.a, body_w)) {
					manual_lines.push_back({0, l, ""});
				}
				break;
			case 1:
				manual_lines.push_back({1, m.a, ""});
				break;
			case 2: {
				const auto w = wrap(0, m.b, body_w - 16);
				for (size_t i = 0; i < w.size(); i++) {
					manual_lines.push_back({i ? 5 : 2, i ? "" : m.a, w[i]});
				}
				break;
			}
			case 3: {
				const auto w = wrap(0, m.b, body_w - col1);
				if (w.empty()) {
					manual_lines.push_back({3, m.a, ""});
				}
				for (size_t i = 0; i < w.size(); i++) {
					manual_lines.push_back({3, i ? "" : m.a, w[i]});
				}
				break;
			}
			default:
				manual_lines.push_back({4, "", ""});
			}
		}
		while (!manual_lines.empty() && manual_lines.back().kind == 4) {
			manual_lines.pop_back();
		}
	}

	constexpr int MAN_TOP = 22, MAN_LH = 14;

	int manual_rows() {
		return (H - 16 - MAN_TOP) / MAN_LH;
	}

	void manual_scroll_by(int d) {
		manual_layout(help_page);
		const int maxs = std::max(0, static_cast<int>(manual_lines.size()) - manual_rows());
		manual_scroll  = std::clamp(manual_scroll + d, 0, maxs);
	}

	void draw_manual() {
		manual_load();
		help_page = std::clamp(help_page, 0, static_cast<int>(manual.size()) - 1);
		manual_layout(help_page);
		target->fill8(C_BG, PW, H, 0, 0);
		target->fill8(C_PANEL, PW, 19, 0, 0);
		text_fit(0, manual[help_page].title, 8, 3, PW - 70);
		char pbuf[16];
		std::snprintf(pbuf, sizeof pbuf, "%d / %d", help_page + 1, static_cast<int>(manual.size()));
		text(0, pbuf, PW - 50, 3);
		const int rows = manual_rows();
		const int n    = static_cast<int>(manual_lines.size());
		manual_scroll  = std::clamp(manual_scroll, 0, std::max(0, n - rows));
		const int col1 = PW >= 400 ? 112 : 90;
		int       y    = MAN_TOP + 2;
		for (int i = manual_scroll; i < n && i < manual_scroll + rows; i++, y += MAN_LH) {
			const MLine& l = manual_lines[i];
			switch (l.kind) {
			case 1:
				text(0, l.a.c_str(), 10, y);
				target->fill8(C_EDGE, std::min(PW - 20, font(0) ? font(0)->get_text_width(l.a.c_str()) + 4 : 100), 1, 10,
							  y + 12);
				break;
			case 2:
			case 5:
				text(0, l.a.c_str(), 12, y);
				text(0, l.b.c_str(), 28, y);
				break;
			case 3:
				text_fit(0, l.a, 10, y, col1 - 14);
				text(0, l.b.c_str(), 10 + col1, y);
				break;
			case 0:
				text(0, l.a.c_str(), 10, y);
				break;
			default:
				break;
			}
		}
		if (n > rows) {    // scroll bar
			const int track = rows * MAN_LH, th = std::max(12, track * rows / n);
			const int ty    = MAN_TOP + 2 + (track - th) * manual_scroll / std::max(1, n - rows);
			target->fill8(C_DIM, 3, track, PW - 5, MAN_TOP + 2);
			target->fill8(C_EDGE, 3, th, PW - 5, ty);
		}
		target->fill8(C_PANEL, PW, 16, 0, H - 16);
		text_centered(0, "D-Pad: up/down scroll, left/right section.  B: close", PW / 2, H - 14, PW - 8);
	}

	// ---- Menu pop-up -----------------------------------------------------------
	enum { ID_CLOSE = 400, ID_PARTY = 410, ID_BTN = 430, ID_GUIDE = 460, ID_SWAP = 461, ID_MAP = 462, ID_STATS = 463 };

	struct MenuBtn {
		int         icon;
		const char* label;
		SDL_Keycode key;
		int         id;
	};

	const MenuBtn menu_btns[12] = {
			{SB_SPELLBOOK, "Spells", SDLK_B, 0},  {SB_NOTEBOOK, "Notes", SDLK_N, 0},   {SB_MAPS, "Map", SDLK_UNKNOWN, ID_MAP},
			{-1, "Stats", SDLK_UNKNOWN, ID_STATS},              {SB_KEYRING, "Keys", SDLK_K, 0},     {SB_FOOD, "Food", SDLK_F, 0},
			{SB_TARGET, "Target", SDLK_T, 0},      {SB_COMBAT, "Combat", SDLK_C, 0},    {SB_DISK, "Save", SDLK_S, 0},
			{-1, "Manual", SDLK_UNKNOWN, ID_GUIDE}, {-1, "Swap screens", SDLK_UNKNOWN, ID_SWAP},
			{-1, "Game menu", SDLK_ESCAPE, 0},
	};

	constexpr int MP_X = 6, MP_Y = 4, MP_W = GAME_W - 12, MP_H = H - 8;

	enum { ID_SEC = 500 };

	void draw_manual_menu() {
		manual_load();
		target->fill8(C_BG, GAME_W, H, 0, 0);
		panel(MP_X, MP_Y, MP_W, MP_H, false);
		target->fill8(C_BG, MP_W - 4, MP_H - 4, MP_X + 2, MP_Y + 2);
		text(0, "Manual", MP_X + 8, MP_Y + 6);
		panel(MP_X + MP_W - 58, MP_Y + 3, 54, 18, pressed_id == ID_CLOSE);
		text_centered(0, "Close", MP_X + MP_W - 31, MP_Y + 5);
		mhits.push_back({MP_X + MP_W - 62, MP_Y, 62, 26, SDLK_UNKNOWN, SDL_KMOD_NONE, ID_CLOSE});
		const int n    = std::min(14, static_cast<int>(manual.size()));
		const int rows = (n + 1) / 2;
		const int gy = MP_Y + 26, bw = (MP_W - 8) / 2, bh = std::min(34, (MP_Y + MP_H - 4 - gy) / std::max(1, rows));
		for (int i = 0; i < n; i++) {
			const int bx = MP_X + 4 + (i % 2) * bw, by = gy + (i / 2) * bh;
			const int id = ID_SEC + i;
			panel(bx + 1, by + 1, bw - 2, bh - 2, i == help_page || pressed_id == id);
			text_centered(0, manual[i].title.c_str(), bx + bw / 2, by + bh / 2 - 6, bw - 8);
			mhits.push_back({bx, by, bw, bh, SDLK_UNKNOWN, SDL_KMOD_NONE, id});
		}
	}

	void draw_menu(Game_window* gwin) {
		Image_buffer8* prev_target = target;
		target                     = mbuf;
		mhits.clear();
		if (help_page >= 0) {    // the manual is open: its sections
			draw_manual_menu();
			target     = prev_target;
			menu_dirty = false;
			return;
		}
		target->fill8(C_BG, GAME_W, H, 0, 0);
		panel(MP_X, MP_Y, MP_W, MP_H, false);
		target->fill8(C_BG, MP_W - 4, MP_H - 4, MP_X + 2, MP_Y + 2);
		text(0, "Menu", MP_X + 8, MP_Y + 6);
		panel(MP_X + MP_W - 58, MP_Y + 3, 54, 18, pressed_id == ID_CLOSE);
		text_centered(0, "Close", MP_X + MP_W - 31, MP_Y + 8);
		mhits.push_back({MP_X + MP_W - 62, MP_Y, 62, 26, SDLK_UNKNOWN, SDL_KMOD_NONE, ID_CLOSE});
		// Party: tap a portrait for that member's inventory (Exult's keys 1-8).
		const std::vector<Actor*> members = party_members(gwin);
		const int                 slot    = (MP_W - 8) / 8;
		for (size_t i = 0; i < members.size(); i++) {
			const int x  = MP_X + 4 + static_cast<int>(i) * slot, y = MP_Y + 26;
			const int id = ID_PARTY + static_cast<int>(i);
			panel(x, y, slot - 2, 44, pressed_id == id);
			draw_face(members[i], x + 1, y + 1, slot - 4, 34);
			bar(x + 3, y + 38, slot - 8, members[i]->get_effective_prop(Actor::health),
				members[i]->get_effective_prop(Actor::strength), C_RED);
			mhits.push_back({x, y, slot - 2, 44, static_cast<SDL_Keycode>(SDLK_1 + i), SDL_KMOD_NONE, id});
		}
		// Commands: 3 x 4 grid.
		const int gy = MP_Y + 74, bw = (MP_W - 8) / 3, bh = (MP_Y + MP_H - 4 - gy) / 4;
		for (int i = 0; i < 12; i++) {
			const MenuBtn& b  = menu_btns[i];
			const int      bx = MP_X + 4 + (i % 3) * bw, by = gy + (i / 3) * bh;
			const int      id = b.id ? b.id : ID_BTN + i;
			const bool     on = (b.key == SDLK_C && gwin->in_combat()) || (b.id == ID_SWAP && game_on_top);
			panel(bx + 1, by + 1, bw - 2, bh - 2, on || pressed_id == id);
			if (b.icon >= 0) {
				Shape_frame* ic = sb_icon(b.icon);
				const int    iw = ic ? std::min(ic->get_width(), 30) : 0;
				target->set_clip(bx + 3, by + 2, iw + 2, bh - 4);
				paint_centered(ic, bx + 4 + iw / 2, by + bh / 2);
				target->clear_clip();
				text_fit(0, b.label, bx + iw + 8, by + bh / 2 - 5, bw - iw - 10);
			} else {
				text_centered(0, b.label, bx + bw / 2, by + bh / 2 - 5, bw - 6);
			}
			mhits.push_back({bx, by, bw, bh, b.key, SDL_KMOD_NONE, id});
		}
		target     = prev_target;
		menu_dirty = false;
	}

	int hit_at(int x, int y) {
		for (size_t i = 0; i < mhits.size(); i++) {
			const Hit& h = mhits[i];
			if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void push_key(SDL_Keycode key, SDL_Keymod mod) {
		SDL_Event e{};
		e.type         = SDL_EVENT_KEY_DOWN;
		e.key.key      = key;
		e.key.mod      = mod;
		e.key.down     = true;
		e.key.scancode = SDL_SCANCODE_UNKNOWN;
		SDL_PushEvent(&e);
		e.type     = SDL_EVENT_KEY_UP;
		e.key.down = false;
		SDL_PushEvent(&e);
	}

	void push_key_fwd(SDL_Keycode key) {
		push_key(key, SDL_KMOD_NONE);
	}

	void set_menu(bool open) {
		if (!open && help_page >= 0 && !game_on_top) {
			help_page = -1;    // the manual's section buttons were the menu
			last_draw = 0;
		}
		if (menu_open != open) {
			menu_open     = open;
			menu_dirty    = true;
			overlay_dirty = true;
			last_draw     = 0;
		}
	}

	int manual_last = 0;    // reopen where it was left

	void open_guide(bool open) {
		if (open) {
			help_page = manual_last;
			map_shown = false;
			if (!game_on_top) {
				menu_open     = false;
				set_menu(true);
			}
		} else {
			if (help_page >= 0) {
				manual_last = help_page;
			}
			const bool was = help_page >= 0;
			help_page      = -1;
			if (was && menu_open) {
				set_menu(false);
			}
		}
		menu_dirty = true;
		last_draw  = 0;
	}

	// ---- D-pad target cycling -------------------------------------------------
	int          pending_cycle = 0;
	Game_object* last_target   = nullptr;

	void do_cycle(Game_window* gwin, int dir) {
		Actor* av = gwin->get_main_actor();
		if (!av) {
			return;
		}
		Game_object_vector found;
		av->find_nearby(found, c_any_shapenum, 14, 0);
		struct T {
			Game_object* obj;
			int          cx, cy, dist, rank;
		};
		std::vector<T> list;
		const int      w = gwin->get_width(), h = gwin->get_height();
		for (Game_object* obj : found) {
			if (obj == av) {
				continue;
			}
			const auto cls = obj->get_info().get_shape_class();
			if (cls == Shape_info::unusable || cls == Shape_info::building) {
				continue;
			}
			if (obj->get_name().empty()) {
				continue;
			}
			Shape_frame* f = obj->get_shape();
			if (!f) {
				continue;
			}
			int x = 0, y = 0;
			gwin->get_shape_location(obj, x, y);
			const int cx = x - f->get_xleft() + f->get_width() / 2;
			const int cy = y - f->get_yabove() + f->get_height() / 2;
			if (cx < 0 || cy < 0 || cx >= w || cy >= h) {
				continue;
			}
			const int  dx = cx - w / 2, dy = cy - h / 2;
			const bool person = obj->get_info().is_npc() && !obj->get_flag(Obj_flags::dead);
			const int  rank   = person ? 0 : cls == Shape_info::container ? 1 : 2;
			list.push_back({obj, cx, cy, dx * dx + dy * dy, rank});
		}
		if (list.empty()) {
			return;
		}
		std::sort(list.begin(), list.end(), [](const T& a, const T& b) {
			return a.rank != b.rank ? a.rank < b.rank : a.dist < b.dist;
		});
		int idx = -1;
		for (size_t i = 0; i < list.size(); i++) {
			if (list[i].obj == last_target) {
				idx = static_cast<int>(i);
			}
		}
		const int n = static_cast<int>(list.size());
		idx         = idx < 0 ? (dir > 0 ? 0 : n - 1) : ((idx + dir) % n + n) % n;
		last_target = list[idx].obj;
		N3DS_SetCursor(static_cast<float>(list[idx].cx + XOFF), static_cast<float>(list[idx].cy));
		last_draw = 0;
	}
}    // namespace

bool n3ds_reading_on_top() {
	return in_game && !game_on_top;
}

void n3ds_reading_page(const void* who, const char* text, int len, bool more) {
	read_who  = who;
	read_sign = false;
	read_text.assign(text ? text : "", text ? std::max(0, len) : 0);
	read_more = more;
	last_draw = 0;
}

void n3ds_reading_sign(const void* who, int gump_shape, int fnt, const std::string* lines, int n, bool serpentine) {
	if (read_who == who && read_sign) {
		return;    // already showing it (the sign repaints often)
	}
	read_who   = who;
	read_sign  = true;
	sign_shape = gump_shape;
	sign_font  = fnt;
	sign_serp  = serpentine;
	sign_lines.assign(lines, lines + std::max(0, n));
	last_draw = 0;
}

void n3ds_reading_end(const void* who) {
	if (who == read_who) {
		read_who  = nullptr;
		read_sign = false;
		read_text.clear();
		last_draw = 0;
	}
}

bool n3ds_conv_on_top() {
	return in_game && !game_on_top;
}

void n3ds_conv_text(int face_shape, int face_frame, int face_file, const char* msg, int len, bool large_face) {
	(void)large_face;
	conv_has        = true;
	conv_face_shape = face_shape;
	conv_face_frame = face_frame;
	conv_face_file  = face_file;
	conv_text.assign(msg ? msg : "", msg ? std::max(0, len) : 0);
	last_draw = 0;
}

bool n3ds_game_on_top() {
	return game_on_top;
}

// Game messages (barks, "Iolo: Hail!", item text) for the dashboard's log.
void n3ds_note_text(const char* msg, Game_object* item) {
	if (!msg || !*msg) {
		return;
	}
	std::string s;
	for (const char* p = msg; *p; p++) {
		if (*p != '@' && *p != '~' && *p != '\r') {
			s += *p == '\n' ? ' ' : *p;
		}
	}
	while (!s.empty() && s.back() == ' ') {
		s.pop_back();
	}
	if (s.empty()) {
		return;
	}
	if (item) {
		if (Actor* a = item->as_actor()) {
			const std::string n = a->get_name();
			if (!n.empty()) {
				s = n + ": " + s;
			}
		}
	}
	if (!messages.empty() && messages.back() == s) {
		return;
	}
	messages.push_back(s);
	while (messages.size() > 12) {
		messages.pop_front();
	}
	last_draw = 0;
}

// --- Hooks called by SDL's 3DS backend ---------------------------------------

extern "C" int N3DS_GameScreen(void) {
	return game_on_top ? GFX_TOP : GFX_BOTTOM;
}

// Keep the C-Stick cursor on the visible game area.
extern "C" void N3DS_ClampCursor(float* x, float* y) {
	(void)y;
	*x = std::clamp(*x, static_cast<float>(XOFF), static_cast<float>(XOFF + GAME_W - 1));
}

// Draw the menu button / menu pop-up over the game on the bottom screen.
// fb: 32-bit, rotated (pixel x,y at fb[x * fbw + (fbw - 1 - y)]).
extern "C" void N3DS_BottomOverlay(void* fbp, int fbw, int fbh) {
	last_present  = SDL_GetTicks();
	overlay_dirty = false;    // this frame shows the current overlay
	if (!in_game || game_on_top || fbw < H || fbh < GAME_W) {
		return;
	}
	auto* fb = static_cast<uint32_t*>(fbp);
	if (cb_mode != 0 && cbuf && pal_loaded) {
		const unsigned char* s  = cbuf->get_bits();
		const int            lw = cbuf->get_line_width();
		for (int x = 0; x < GAME_W; x++) {
			uint32_t* col = fb + x * fbw + (fbw - 1);
			for (int y = cb_y0; y < H; y++) {
				*(col - y) = rgb32[s[y * lw + x]];
			}
		}
		return;    // no menu button during conversations and reading
	}
	if (menu_open && mbuf && pal_loaded) {
		const unsigned char* s  = mbuf->get_bits();
		const int            lw = mbuf->get_line_width();
		for (int x = 0; x < GAME_W; x++) {
			uint32_t* col = fb + x * fbw + (fbw - 1);
			for (int y = 0; y < H; y++) {
				uint32_t& d = *(col - y);
				if (x >= MP_X && x < MP_X + MP_W && y >= MP_Y && y < MP_Y + MP_H) {
					d = rgb32[s[y * lw + x]];
				} else {
					d = ((d >> 1) & 0x7F7F7F00) | 0xFF;    // dim the game behind
				}
			}
		}
		return;
	}
	// Menu button: translucent dark box with three light bars.
	const bool pressed = pressed_id == ID_MENU_BUTTON;
	for (int x = MB_X; x < MB_X + MB_W; x++) {
		uint32_t* col = fb + x * fbw + (fbw - 1);
		for (int y = MB_Y; y < MB_Y + MB_H; y++) {
			uint32_t&  d    = *(col - y);
			const int  ry   = y - MB_Y;
			const bool edge = x == MB_X || x == MB_X + MB_W - 1 || y == MB_Y || y == MB_Y + MB_H - 1;
			const bool bars = x >= MB_X + 7 && x < MB_X + MB_W - 7 && ry >= 6 && ry < 16 && (ry - 6) % 4 < 2;
			if (bars || edge) {
				d = pressed ? 0xF0D060FF : 0xE6E6E6FF;
			} else {
				d = ((d >> 2) & 0x3F3F3F00) | 0xFF;
			}
		}
	}
}

// Which part of the window the touchscreen shows: the game area, or with zoom
// a smaller rect around the Avatar.
extern "C" void N3DS_BottomView(int* sx, int* sy, int* sw, int* sh) {
	Game_window* gwin = Game_window::get_instance();
	if (!in_game || zoom == 0 || !gwin || !gwin->get_main_actor()) {
		view_x = 0, view_y = 0, view_w = GAME_W, view_h = H;
	} else {
		int ax = GAME_W / 2, ay = H / 2;
		gwin->get_shape_location(gwin->get_main_actor(), ax, ay);
		view_w = zoom_w[zoom];
		view_h = zoom_h[zoom];
		view_x = std::clamp(ax - view_w / 2, 0, GAME_W - view_w);
		view_y = std::clamp(ay - 10 - view_h / 2, 0, H - view_h);
	}
	*sx = view_x + XOFF;
	*sy = view_y;
	*sw = view_w;
	*sh = view_h;
}

extern "C" void N3DS_CycleTarget(int dir) {
	pending_cycle += dir;
}

// Buttons taken for the layout's own UI.
extern "C" u32 N3DS_KeyHook(u32 kDown, u32 kHeld) {
	if (!in_game) {
		return 0;
	}
	u32        taken = 0;
	const bool l     = (kHeld & KEY_L) != 0;
	if (l && (kDown & KEY_START)) {
		pending_swap = true;
		taken |= KEY_START;
	}
	if (!l && (kDown & KEY_ZL) && !game_on_top) {
		zoom      = (zoom + 1) % ZOOM_STEPS;
		last_draw = 0;
		taken |= KEY_ZL;
		N3DS_Represent();
	}
	if (!l && (kDown & KEY_DUP) && help_page < 0) {
		pending_map = true;
		taken |= KEY_DUP;
	}
	if (!l && (kDown & KEY_DDOWN) && help_page < 0) {
		pending_stats = true;
		taken |= KEY_DDOWN;
	}
	if (stats_shown && !l && (kDown & KEY_B)) {
		pending_stats = true;
		taken |= KEY_B;
	}
	if (map_shown && !l && (kDown & KEY_B)) {
		pending_map = true;
		taken |= KEY_B;
	}
	if (l && (kDown & KEY_Y)) {
		open_guide(help_page < 0);
		taken |= KEY_Y;
	}
	if (help_page >= 0 && !l) {
		manual_load();
		const int n = static_cast<int>(manual.size());
		if (kDown & KEY_DUP) {
			manual_scroll_by(-3);
		}
		if (kDown & KEY_DDOWN) {
			manual_scroll_by(3);
		}
		if (kDown & (KEY_DLEFT | KEY_DRIGHT)) {
			help_page     = std::clamp(help_page + ((kDown & KEY_DRIGHT) ? 1 : -1), 0, n - 1);
			manual_scroll = 0;
			menu_dirty    = true;
		}
		if (kDown & (KEY_B | KEY_START)) {
			open_guide(false);
		}
		last_draw = 0;
		taken |= kDown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_B | KEY_START);
	} else if (menu_open && (kDown & (KEY_B | KEY_START))) {
		set_menu(false);
		taken |= kDown & (KEY_B | KEY_START);
	}
	return taken;
}

void n3ds_bottom_init() {}

void n3ds_bottom_tick();

// Called by SDL's input polling, so the dashboard keeps working while the
// game is paused (an open inventory pauses Ultima VII: no frames are drawn).
extern "C" void N3DS_InputTick() {
	static bool busy = false;
	if (!busy) {
		busy = true;
		n3ds_bottom_tick();
		busy = false;
	}
}

void n3ds_bottom_tick() {
	n3ds::stereo_tick();
	Game_window* gwin  = Game_window::get_instance();
	const bool   ready = gwin && gwin->get_main_actor() && gwin->get_pal() && Shape_manager::get_instance();
	if (!ready) {
		// Exult's menus (on the touchscreen): keep the legend on the top screen.
		if (!in_game && gfxGetScreenFormat(GFX_TOP) != GSP_RGB565_OES) {
			n3ds_show_legend();
		}
		return;
	}
	const uint64_t now = SDL_GetTicks();
	if (!in_game) {
		in_game = true;
		unsigned long       cached_kb = 0;
		const unsigned long read_kb   = n3ds_cache_report(&cached_kb);
		std::printf("[exult-3ds] in game: %.1f MB read whole from SD, %.1f MB cached, heap used %d MB\n",
					read_kb / 1024.0, cached_kb / 1024.0, n3ds_heap_used_mb());
		char io[160];
		n3ds_io_report(io, sizeof io);
		std::printf("[exult-3ds] %s\n", io);
		n3ds_sd_report(io, sizeof io);
		std::printf("[exult-3ds] %s\n", io);
		apply_layout();
	}
	if (pending_swap) {
		pending_swap = false;
		game_on_top  = !game_on_top;
		std::printf("[exult-3ds] game on the %s screen\n", game_on_top ? "top" : "bottom");
		gwin->set_all_dirty();
		apply_layout();
	} else if (!layout_ok()) {
		apply_layout();    // something (a video mode change) reset the screens
	}
	{
		static uint64_t last_flush = 0;
		if (now - last_flush > 2000) {
			char wr[200];
			if (n3ds_write_report(wr, sizeof wr)) {
				std::printf("[exult-3ds] %s\n", wr);
				n3ds_sd_report(wr, sizeof wr);
				std::printf("[exult-3ds] %s\n", wr);
			}
			n3ds_log_flush();
			last_flush = now;
		}
	}
	if (pending_map) {
		pending_map = false;
		stats_shown = false;
		toggle_world_map(gwin);
	}
	if (pending_stats) {
		pending_stats = false;
		stats_shown   = !stats_shown;
		if (stats_shown) {
			map_shown = false;
			help_page = -1;
		}
		last_draw = 0;
	}
	if (pending_cycle != 0) {
		const int dir = pending_cycle > 0 ? 1 : -1;
		pending_cycle = 0;
		do_cycle(gwin, dir);
	}

	Conversation* conv = nullptr;
	if (Usecode_machine* uc = gwin->get_usecode()) {
		conv = uc->get_conversation();
	}
	// Conversation / reading state for the top screen and the touchscreen panel.
	{
		const bool talking = conv && (conv->get_num_faces_on_screen() > 0 || conv->are_choices_active());
		if (!talking && conv_has) {
			conv_has  = false;
			last_draw = 0;
		}
		conv_choices   = conv && conv->are_choices_active() && conv->get_num_answers() > 0;
		const int mode = game_on_top ? 0 : conv_choices ? 1 : (conv_has || read_who) ? 2 : 0;
		std::string sig = std::to_string(mode) + "/" + std::to_string(answer_page) + "/" + std::to_string(cpressed) + "/"
						  + (read_who ? "r" : "");
		if (mode == 1) {
			for (int i = 0; i < conv->get_num_answers(); i++) {
				sig += conv->get_answer(i);
				sig += '|';
			}
		} else {
			answer_page = 0;
		}
		if (sig != conv_sig && pal_loaded) {
			conv_sig = sig;
			cb_mode  = mode;
			if (!cbuf) {
				cbuf = new Image_buffer8(GAME_W, H);
			}
			Image_buffer8* prev = Shape_frame::get_render_target();
			Shape_frame::set_to_render(cbuf);
			draw_answer_panel(conv);
			Shape_frame::set_to_render(prev);
			if (mode == 0) {
				cb_y0 = H;
			}
			overlay_dirty = true;
			if (mode != 0 && menu_open) {
				set_menu(false);
			}
		}
	}
	// Autosave (quicksave into the game in progress) every 15 minutes while
	// idle, so a flat battery or a crash costs at most a few minutes.
	{
		static uint64_t next_autosave = 0;
		if (next_autosave == 0) {
			next_autosave = now + N3DS_AUTOSAVE_MS;
		}
		Gump_manager* gm   = gwin->get_gump_man();
		const bool    idle = !(conv && conv->are_choices_active()) && !(gm && gm->showing_gumps()) && !gwin->in_combat();
		if (now >= next_autosave && idle) {
			push_key(SDLK_S, SDL_KMOD_LCTRL);
			next_autosave = now + N3DS_AUTOSAVE_MS;
			std::printf("[exult-3ds] autosave\n");
		}
	}

	// A changed overlay waits for the game's next frame (presenting twice in
	// one frame made the screen flicker); if the game is paused and no frame
	// comes, show it ourselves. N3DS_BottomOverlay clears the flag.
	if (overlay_dirty && !game_on_top && now - last_present > 120) {
		N3DS_Represent();
	}
	const bool redraw = now - last_draw >= 250;
	if (!redraw && !overlay_dirty && !(menu_open && menu_dirty)) {
		return;
	}
	Palette* pal = gwin->get_pal();
	if (!day_pal_cached) {
		// Use the daylight palette for the UI so it stays readable at night.
		load_palette(gwin);
		day_pal_cached = pal->get_palette_index() == 0 && pal->get_brightness() == 100;
	}
	if (!buf) {
		buf = new Image_buffer8(MAX_PW, H);
	}
	if (!mbuf) {
		mbuf = new Image_buffer8(GAME_W, H);
	}
	// Exult's font code paints into the shared render target: point it at
	// our buffers while drawing.
	Image_buffer8* prev = Shape_frame::get_render_target();
	static uint64_t menu_drawn = 0;
	if (menu_open && (menu_dirty || now - menu_drawn > 1000)) {
		menu_drawn = now;
		Shape_frame::set_to_render(mbuf);
		draw_menu(gwin);
		overlay_dirty = true;
	}
	if (redraw) {
		last_draw = now;
		Shape_frame::set_to_render(buf);
		target = buf;
		tips_update(gwin, conv, now);
		target->fill8(C_BG, MAX_PW, H, 0, 0);    // nothing left over from the guide or tips
		if (help_page >= 0) {
			draw_manual();
		} else if (read_who && !game_on_top) {
			draw_reading();
		} else if (conv_has && !game_on_top) {
			draw_conversation();
		} else if (map_shown) {
			draw_world_map(gwin);
		} else if (stats_shown) {
			draw_stats(gwin);
		} else {
			draw_party(gwin);
			draw_minimap(gwin);
			draw_info(gwin);
			draw_tip();
		}
		if (game_on_top) {
			draw_menu_button();
		}
		blit_panel();
	} else if (game_on_top && overlay_dirty) {
		blit_panel();    // menu opened/closed on the bottom dashboard
	}
	Shape_frame::set_to_render(prev);
	if (overlay_dirty && game_on_top) {
		overlay_dirty = false;
	}
}

// Touch hook called from SDL's N3DS input layer (main thread).
// phase: 0 = down, 1 = move, 2 = up. Returns 1 if the touch belongs to the
// menu, or 0 with nx/ny = where to put the mouse in the window (0..1).
// The touchscreen shows the game 1:1 (or, swapped, sits under it 1:1).
extern "C" int N3DS_BottomTouch(int phase, int x, int y, float* nx, float* ny) {
	auto to_game = [&]() {
		const int cx = std::clamp(x, 0, GAME_W - 1), cy = std::clamp(y, 0, H - 1);
		const float gx = game_on_top ? cx : view_x + (cx + 0.5f) * view_w / GAME_W;
		const float gy = game_on_top ? cy : view_y + (cy + 0.5f) * view_h / H;
		*nx = (gx + XOFF) / WIN_W;
		*ny = gy / H;
		return 0;
	};
	if (!in_game) {
		return to_game();
	}
	// Answer buttons (conversations).
	static bool answer_touch = false;
	if (phase == 0) {
		answer_touch = cb_mode == 1 && !game_on_top && y >= cb_y0;
	}
	if (answer_touch) {
		auto find = [&]() {
			for (const auto& h : chits) {
				if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
					return &h;
				}
			}
			return static_cast<const CHit*>(nullptr);
		};
		const CHit* h = find();
		if (phase == 0) {
			cpressed = h ? h->answer : -2;
		} else if (phase == 2) {
			if (h && h->answer == cpressed) {
				if (h->answer < 0) {
					answer_page++;
				} else {
					static const char keys[] = "123456789abcdefghijklmnopqrstuvwxyz";
					push_key(static_cast<SDL_Keycode>(h->answer < 35 ? keys[h->answer] : '1'), SDL_KMOD_NONE);
				}
			}
			cpressed     = -2;
			answer_touch = false;
		}
		return 1;
	}
	const bool on_button = cb_mode == 0 && x >= MB_X - 4 && y < MB_Y + MB_H + 4;
	if (phase == 0) {
		touch_ui = menu_open || on_button;
		if (touch_ui) {
			touch_hit     = menu_open ? hit_at(x, y) : -1;
			pressed_id    = menu_open ? (touch_hit >= 0 ? mhits[touch_hit].id : -1) : ID_MENU_BUTTON;
			menu_dirty    = true;
			overlay_dirty = true;
			last_draw     = 0;
		}
	}
	if (!touch_ui) {
		return to_game();
	}
	if (phase == 2) {
		if (!menu_open) {
			if (on_button) {
				set_menu(true);
			}
		} else {
			const int now_hit = hit_at(x, y);
			if (touch_hit >= 0 && now_hit == touch_hit) {
				const Hit h = mhits[touch_hit];
				if (h.id >= ID_SEC && help_page >= 0) {    // a manual section
					help_page     = h.id - ID_SEC;
					manual_scroll = 0;
					menu_dirty    = true;
					last_draw     = 0;
					pressed_id    = -1;
					touch_hit     = -1;
					touch_ui      = false;
					overlay_dirty = true;
					return 1;
				}
				if (h.id == ID_CLOSE && help_page >= 0) {
					open_guide(false);
				}
				set_menu(false);
				if (h.id == ID_GUIDE) {
					open_guide(true);
				} else if (h.id == ID_MAP) {
					pending_map = true;
				} else if (h.id == ID_STATS) {
					pending_stats = true;
				} else if (h.id == ID_SWAP) {
					pending_swap = true;
				} else if (h.key != SDLK_UNKNOWN) {
					push_key(h.key, h.mod);
				}
			} else if (touch_hit < 0 && (x < MP_X || x >= MP_X + MP_W || y < MP_Y || y >= MP_Y + MP_H)) {
				set_menu(false);    // tapped outside the menu
			}
		}
		pressed_id    = -1;
		touch_hit     = -1;
		touch_ui      = false;
		menu_dirty    = true;
		overlay_dirty = true;
		last_draw     = 0;
	}
	return 1;
}
