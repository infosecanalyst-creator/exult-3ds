/*
 * Exult for New Nintendo 3DS - bottom-screen companion.
 *
 * While a game is running, the bottom screen becomes:
 *   - Party strip (top):    a portrait per party member with HP/mana bars and
 *                           status colouring. Tap = that member's inventory.
 *   - Middle:               a touchpad for the top-screen cursor, showing the
 *                           game time and the name of whatever is under the
 *                           cursor. During conversations it turns into large
 *                           answer buttons. "?" shows the controls.
 *                           "map" turns it into a live minimap of the area
 *                           around the party; tap a spot to walk there.
 *   - Action bar (bottom):  Map, Spells, Notes, Keys, Pack / Combat, Target,
 *                           Food, Save, Menu - using Exult's shortcut-bar art.
 *
 * Everything is drawn with Exult's own shapes, fonts and palette into an
 * 8-bit buffer and converted to the bottom framebuffer (RGB565) ~10x/sec.
 * Touches arrive from SDL's N3DS virtual-input layer via N3DS_BottomTouch().
 *
 * GPL v2 (same as Exult).
 */
#include <3ds.h>

#include "Gump.h"
#include "Gump_manager.h"
#include "actors.h"
#include "combat_opts.h"
#include "chunkter.h"
#include "conversation.h"
#include "effects.h"
#include "gamemap.h"
#include "schedule.h"
#include "font.h"
#include "gameclk.h"
#include "gamewin.h"
#include "ibuf8.h"
#include "mouse.h"
#include "palette.h"
#include "party.h"
#include "shapeid.h"
#include "shapeinf.h"
#include "shapeinf/npcdollinf.h"
#include "ucmachine.h"
#include "exult_constants.h"
#include "vgafile.h"
#include "n3ds_stereo.h"

#ifdef __GNUC__
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <SDL3/SDL.h>
#ifdef __GNUC__
#	pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef N3DS_AUTOSAVE_MS
#	define N3DS_AUTOSAVE_MS (15 * 60 * 1000)
#endif

extern "C" void N3DS_SetCursor(float x, float y);
extern "C" void n3ds_log_flush();

namespace {
	constexpr int W = 320;
	constexpr int H = 240;

	constexpr int PARTY_Y = 0, PARTY_H = 46;
	constexpr int PAD_Y = PARTY_H, PAD_H = 136;
	constexpr int BAR_Y = PAD_Y + PAD_H, BAR_H = H - BAR_Y;    // 58
	// Middle area: a live picture of the top screen at the left (tap where you
	// see), aspect-correct (400x240 scaled to 227x136), and an info/button
	// column at the right.
	constexpr int VIEW_W = (400 * PAD_H + 120) / 240;    // 227
	constexpr int SIDE_X = VIEW_W + 1;
	constexpr int SIDE_W = W - SIDE_X;

	constexpr int SB_COMBAT = 0, SB_DISK = 1, SB_KEYRING = 3, SB_SPELLBOOK = 4, SB_TARGET = 5, SB_FOOD = 6, SB_MAPS = 8,
				  SB_NOTEBOOK = 9, SB_BACKPACK = 10;

	struct Hit {
		int         x, y, w, h;
		SDL_Keycode key;
		SDL_Keymod  mod;
		int         id;    // >= 0: button id for highlight
	};

	enum Mode { LEGEND, GAME };

	Mode                mode = LEGEND;
	Image_buffer8*      buf  = nullptr;
	uint16_t            rgb[256];
	bool                day_pal_cached = false;
	uint64_t            last_draw      = 0;
	std::vector<Hit>    hits;           // rebuilt each draw, read by the touch hook
	bool                choices_shown = false;
	int                 answer_page   = 0;
	bool                show_help     = false;
	int                 pressed_id    = -1;
	int                 touch_hit     = -1;    // index into hits for the current UI touch
	bool                touch_ui      = false;
	bool                minimap_on    = false;
	bool                view_visible  = false;    // live top-screen picture is showing
	uint16_t            view_px[PAD_H * VIEW_W];    // RGB565, filled from the top screen
	bool                view_new      = false;
	uint64_t            view_last     = 0;
	// Hold L: the live view zooms 2x around where the cursor was, for
	// precise taps on small things (items in backpacks).
	bool                zoom_on       = false;
	int                 zoom_x0 = 0, zoom_y0 = 0;
	constexpr int       ZOOM_W = 200, ZOOM_H = 120;
	// Dragging an item onto a party portrait: that member's inventory opens
	// on the top screen and the cursor moves onto it, so letting go drops the
	// item there (Exult's own rules for weight, theft and equipping apply).
	int                 drop_member   = -1;
	int                 party_count   = 0;
	unsigned char       C_BG, C_PANEL, C_PANEL_HI, C_EDGE, C_RED, C_BLUE, C_GREEN, C_DIM, C_TEXT;

	unsigned char nearest(int r, int g, int b) {
		int best = 0, bestd = 1 << 30;
		for (int i = 0; i < 256; i++) {
			const int cr = (rgb[i] >> 11) << 3, cg = ((rgb[i] >> 5) & 63) << 2, cb = (rgb[i] & 31) << 3;
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
	}

	void blit_view(uint16_t* fb) {
		for (int y = 0; y < PAD_H; y++) {
			const uint16_t* row = view_px + y * VIEW_W;
			uint16_t*       col = fb + (239 - (PAD_Y + y));
			for (int x = 0; x < VIEW_W; x++) {
				col[x * 240] = row[x];
			}
		}
		view_new = false;
	}

	void blit() {
		auto*                fb  = reinterpret_cast<uint16_t*>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr));
		const unsigned char* src = buf->get_bits();
		const int            lw  = buf->get_line_width();
		for (int y = 0; y < H; y++) {
			const unsigned char* row = src + y * lw;
			for (int x = 0; x < W; x++) {
				fb[x * 240 + (239 - y)] = rgb[row[x]];
			}
		}
		if (view_visible) {
			blit_view(fb);
		}
		GSPGPU_FlushDataCache(fb, W * H * 2);
	}

	Shape_frame* sb_icon(int shape) {
		ShapeID sid(shape, 0, SF_SHORTCUTBAR_VGA);
		return sid.get_shape();
	}

	void paint_centered(Shape_frame* f, int cx, int cy) {
		if (f) {
			f->paint_rle(buf, cx - f->get_width() / 2 + f->get_xleft(), cy - f->get_height() / 2 + f->get_yabove());
		}
	}

	std::shared_ptr<Font> font(int n) {
		return Shape_manager::get_instance()->get_font(n);
	}

	void text(int fontnum, const char* s, int x, int y) {
		if (auto f = font(fontnum)) {
			f->paint_text(buf, s, x, y);
		}
	}

	void text_centered(int fontnum, const char* s, int cx, int y, int maxw = W) {
		auto f = font(fontnum);
		if (!f) {
			return;
		}
		std::string str(s);
		while (!str.empty() && f->get_text_width(str.c_str()) > maxw) {
			str.pop_back();
		}
		f->paint_text(buf, str.c_str(), cx - f->get_text_width(str.c_str()) / 2, y);
	}

	// Word-wrapped text, at most max_lines lines; returns the y after it.
	int text_wrapped(int fontnum, const std::string& s, int x, int y, int maxw, int max_lines, int line_h) {
		auto f = font(fontnum);
		if (!f) {
			return y;
		}
		std::string line, word;
		int         lines = 0;
		auto        flush = [&]() {
            if (!line.empty() && lines < max_lines) {
                f->paint_text(buf, line.c_str(), x, y);
                y += line_h;
                lines++;
            }
            line.clear();
		};
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
				flush();
				line = word;
				while (line.size() > 1 && f->get_text_width(line.c_str()) > maxw) {
					line.pop_back();
				}
			}
			word.clear();
		}
		flush();
		return y;
	}

	void panel(int x, int y, int w, int h, bool hi) {
		buf->fill8(C_EDGE, w, h, x, y);
		buf->fill8(hi ? C_PANEL_HI : C_PANEL, w - 2, h - 2, x + 1, y + 1);
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

	void bar(int x, int y, int w, int val, int maxv, unsigned char col) {
		buf->fill8(C_DIM, w, 3, x, y);
		int f = maxv > 0 ? val * w / maxv : 0;
		if (f > w) {
			f = w;
		}
		if (f > 0) {
			buf->fill8(col, f, 3, x, y);
		}
	}

	void draw_party(Game_window* gwin) {
		buf->fill8(C_BG, W, PARTY_H, 0, PARTY_Y);
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
		const int slot = W / 8;
		for (size_t i = 0; i < members.size(); i++) {
			Actor*    a  = members[i];
			const int sx = static_cast<int>(i) * slot;
			const int id = 100 + static_cast<int>(i);
			const bool hurt = a->get_flag(Obj_flags::poisoned) || a->was_hit();
			panel(sx + 1, PARTY_Y + 1, slot - 2, PARTY_H - 2, pressed_id == id);
			if (hurt) {
				buf->fill8(a->get_flag(Obj_flags::poisoned) ? C_GREEN : C_RED, slot - 4, 2, sx + 2, PARTY_Y + 2);
			}
			buf->set_clip(sx + 2, PARTY_Y + 2, slot - 4, 32);
			if (Shape_frame* f = face_of(a)) {
				paint_centered(f, sx + slot / 2, PARTY_Y + 19);
			} else {
				const std::string n = a->get_name();
				text_centered(0, n.substr(0, 1).c_str(), sx + slot / 2, PARTY_Y + 10);
			}
			buf->clear_clip();
			bar(sx + 4, PARTY_Y + 35, slot - 8, a->get_effective_prop(Actor::health), a->get_effective_prop(Actor::strength), C_RED);
			if (a->get_effective_prop(Actor::magic) > 0) {
				bar(sx + 4, PARTY_Y + 40, slot - 8, a->get_effective_prop(Actor::mana), a->get_effective_prop(Actor::magic), C_BLUE);
			}
			// Status tag in the portrait's corner.
			const char*   tag = nullptr;
			unsigned char tc  = C_TEXT;
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
				buf->fill8(tc, 22, 9, sx + 2, PARTY_Y + 23);
				text(5, tag, sx + 3, PARTY_Y + 24);
			}
			if (drop_member == static_cast<int>(i)) {    // item being dragged here
				buf->fill8(C_TEXT, slot - 2, 2, sx + 1, PARTY_Y + 1);
				buf->fill8(C_TEXT, slot - 2, 2, sx + 1, PARTY_Y + PARTY_H - 3);
			}
			// Tap = inventory of party member i (Exult binds keys 1-8 to this)
			hits.push_back({sx, PARTY_Y, slot, PARTY_H, static_cast<SDL_Keycode>(SDLK_1 + i), SDL_KMOD_NONE, id});
		}
		party_count = static_cast<int>(members.size());

		// Spare room in the strip: spell out what needs attention.
		const int free_x = party_count * slot + 4;
		if (free_x < W - 60) {
			std::vector<std::string> alerts;
			for (Actor* a : members) {
				const std::string n    = a->get_name();
				const int         food = a->get_property(static_cast<int>(Actor::food_level));
				const int         hp = a->get_effective_prop(Actor::health), maxhp = a->get_effective_prop(Actor::strength);
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
				text(5, "Party OK", free_x + 2, PARTY_Y + 4);
			}
			int y = PARTY_Y + 4;
			for (size_t k = 0; k < alerts.size() && k < 4; k++, y += 10) {
				std::string t = alerts[k];
				auto        f = font(5);
				while (f && t.size() > 3 && f->get_text_width(t.c_str()) > W - free_x - 4) {
					t.pop_back();
				}
				text(5, t.c_str(), free_x + 2, y);
			}
		}
	}

	// ---- Minimap ------------------------------------------------------------
	// Terrain colours (one per tile, sampled from the middle of each 8x8 tile
	// of the chunk's rendered ground), drawn at 2x2 pixels per tile and
	// centred on the Avatar, with party / NPC dots and the top screen's view
	// outlined. Tap a spot to walk there (Exult's pathfinding).
	constexpr int MM_SCALE = 2;
	constexpr int MM_W     = VIEW_W;    // same area as the live view
	constexpr int MM_H     = PAD_H - 2;
	constexpr int MM_TW = MM_W / MM_SCALE, MM_TH = MM_H / MM_SCALE;

	std::unordered_map<int, std::vector<unsigned char>> terrain_thumbs;
	std::vector<unsigned char>                          mm_tiles(MM_TW * MM_TH);
	int                                                 mm_ox = 0, mm_oy = 0, mm_map = -1;
	uint64_t                                            mm_built   = 0;
	int                                                 pending_tx = -1, pending_ty = -1;
	std::string                                         mm_msg;
	uint64_t                                            mm_msg_until = 0;

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

	void build_minimap(Game_window* gwin, int ox, int oy) {
		Game_map* gmap = gwin->get_map();
		for (int y = 0; y < MM_TH; y++) {
			const int ty = (oy + y + c_num_tiles) % c_num_tiles;
			for (int x = 0; x < MM_TW; x++) {
				const int   tx   = (ox + x + c_num_tiles) % c_num_tiles;
				const int   tnum = gmap->get_terrain_num(tx / c_tiles_per_chunk, ty / c_tiles_per_chunk);
				const auto& th   = terrain_thumb(tnum);
				mm_tiles[y * MM_TW + x] = th[(ty % c_tiles_per_chunk) * c_tiles_per_chunk + tx % c_tiles_per_chunk];
			}
		}
	}

	void mm_dot(int x, int y, unsigned char col, int size) {
		buf->fill8(C_BG, size + 2, size + 2, x - 1, y - 1);
		buf->fill8(col, size, size, x, y);
	}

	void draw_minimap(Game_window* gwin) {
		Actor*           av     = gwin->get_main_actor();
		const Tile_coord at     = av->get_tile();
		const int        ox     = at.tx - MM_TW / 2;
		const int        oy     = at.ty - MM_TH / 2;
		const uint64_t   now    = SDL_GetTicks();
		const int        mapnum = gwin->get_map()->get_num();
		if (mapnum != mm_map || ox != mm_ox || oy != mm_oy || now - mm_built > 3000) {
			build_minimap(gwin, ox, oy);
			mm_ox    = ox;
			mm_oy    = oy;
			mm_map   = mapnum;
			mm_built = now;
		}
		const int      x0   = 1, y0 = PAD_Y + 1;
		unsigned char* bits = buf->get_bits();
		const int      lw   = buf->get_line_width();
		for (int y = 0; y < MM_TH; y++) {
			for (int x = 0; x < MM_TW; x++) {
				const unsigned char c = mm_tiles[y * MM_TW + x];
				unsigned char*      p = bits + (y0 + y * MM_SCALE) * lw + x0 + x * MM_SCALE;
				p[0] = p[1] = p[lw] = p[lw + 1] = c;
			}
		}
		auto to_px = [&](int tx, int ty, int& px, int& py) {
			px = x0 + Tile_coord::delta(ox, tx) * MM_SCALE;
			py = y0 + Tile_coord::delta(oy, ty) * MM_SCALE;
			return px >= x0 && py >= y0 && px < x0 + MM_W - 2 && py < y0 + MM_H - 2;
		};
		// What the top screen shows.
		{
			int px, py;
			to_px(gwin->get_scrolltx(), gwin->get_scrollty(), px, py);
			const int vw = gwin->get_width() / c_tilesize * MM_SCALE;
			const int vh = gwin->get_height() / c_tilesize * MM_SCALE;
			buf->set_clip(x0, y0, MM_W, MM_H);
			buf->fill8(C_TEXT, vw, 1, px, py);
			buf->fill8(C_TEXT, vw, 1, px, py + vh - 1);
			buf->fill8(C_TEXT, 1, vh, px, py);
			buf->fill8(C_TEXT, 1, vh, px + vw - 1, py);
			buf->clear_clip();
		}
		// People nearby: yellow (red if hostile), party green, Avatar white.
		std::vector<Actor*> npcs;
		gwin->get_nearby_npcs(npcs);
		static unsigned char C_YELLOW = 0;
		if (!C_YELLOW) {
			C_YELLOW = nearest(240, 220, 60);
		}
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
				mm_dot(px, py, party ? C_GREEN : foe ? C_RED : C_YELLOW, 2);
			}
		}
		int ax, ay;
		if (to_px(at.tx, at.ty, ax, ay)) {
			mm_dot(ax - 1, ay - 1, C_TEXT, 4);
		}
		if (!mm_msg.empty() && now < mm_msg_until) {
			buf->fill8(C_BG, MM_W - 40, 14, x0 + 20, PAD_Y + PAD_H - 19);
			text_centered(0, mm_msg.c_str(), x0 + MM_W / 2, PAD_Y + PAD_H - 17, MM_W - 48);
		} else {
			buf->fill8(C_BG, 92, 11, 3, PAD_Y + 3);
			text(5, "tap to walk there", 6, PAD_Y + 4);
		}
	}

	void travel_to(Game_window* gwin, int tx, int ty) {
		Main_actor* av = gwin->get_main_actor();
		mm_msg_until   = SDL_GetTicks() + 2500;
		if (av->get_flag(Obj_flags::asleep) || av->get_flag(Obj_flags::paralyzed)
			|| av->get_schedule_type() == Schedule::sleep || gwin->get_moving_barge() || av->in_usecode_control()) {
			mm_msg = "Can't walk right now";
			return;
		}
		const Tile_coord dest(tx, ty, av->get_lift());
		const int        speed = Mouse::mouse() ? Mouse::mouse()->avatar_speed : gwin->get_std_delay();
		if (av->walk_path_to_tile(dest, speed)) {
			av->get_followers();
			gwin->get_effects()->add_effect(std::make_unique<Sprites_effect>(18, dest, 0, 0, 0, 0));
			mm_msg = "On the way";
		} else {
			mm_msg = "No way to get there";
		}
	}
	void draw_pad(Game_window* gwin, Conversation* conv) {
		view_visible = false;
		buf->fill8(C_DIM, W, PAD_H, 0, PAD_Y);
		buf->fill8(C_EDGE, W, 1, 0, PAD_Y);

		choices_shown = conv && conv->are_choices_active() && conv->get_num_answers() > 0;
		if (choices_shown) {
			const int n       = conv->get_num_answers();
			const int perpage = n > 10 ? 9 : 10;
			if (answer_page * perpage >= n) {
				answer_page = 0;
			}
			const int first = answer_page * perpage;
			const int bw = W / 2 - 6, bh = 24;
			for (int k = 0; k < perpage && first + k < n; k++) {
				const int i  = first + k;
				const int bx = 4 + (k % 2) * (W / 2), by = PAD_Y + 6 + (k / 2) * (bh + 2);
				const int id = 200 + k;
				panel(bx, by, bw, bh, pressed_id == id);
				text_centered(0, conv->get_answer(i), bx + bw / 2, by + 6, bw - 8);
				static const char keys[] = "123456789abcdefghijklmnopqrstuvwxyz";
				const char        c      = i < 35 ? keys[i] : '1';
				hits.push_back({bx, by, bw, bh, static_cast<SDL_Keycode>(c), SDL_KMOD_NONE, id});
			}
			if (n > perpage) {
				const int bx = 4 + (perpage % 2) * (W / 2), by = PAD_Y + 6 + (perpage / 2) * (bh + 2);
				panel(bx, by, bw, bh, pressed_id == 299);
				text_centered(0, "More...", bx + bw / 2, by + 6, bw - 8);
				hits.push_back({bx, by, bw, bh, SDLK_UNKNOWN, SDL_KMOD_NONE, 299});
			}
			return;
		}
		answer_page = 0;

		if (show_help) {
			static const char* lines[] = {"Tap the picture: click there   Double-tap: use/talk",
										  "Hold the stylus still: walk there   Drag: move items",
										  "C-Stick cursor  Circle Pad walk  A click  X use",
										  "B walk to cursor   Y inventory   R combat",
										  "D-Pad: up Map, down Stats, left/right next target",
										  "ZL keys   ZR target   START menu   SELECT save",
										  "Hold L + D-Pad: Watch Pause Lockpick Food",
										  "L+SELECT quick-restore   L+X next song",
										  "Portrait: inventory. Drag an item onto it: give",
										  "Hold L: zoom x2   map: minimap   3D slider: depth"};
			int y = PAD_Y + 6;
			for (const char* l : lines) {
				text(5, l, 6, y);
				y += 13;
			}
			hits.push_back({0, PAD_Y, W, PAD_H, SDLK_UNKNOWN, SDL_KMOD_NONE, 298});    // tap closes
			return;
		}

		// Right-hand column: time, combat state, what the cursor is on, and
		// the "?" / "map" buttons.
		buf->fill8(C_BG, SIDE_W, PAD_H - 1, SIDE_X, PAD_Y + 1);
		buf->fill8(C_EDGE, 1, PAD_H - 1, SIDE_X - 1, PAD_Y + 1);
		const int sx = SIDE_X + 4, sw = SIDE_W - 8;
		if (Game_clock* clk = gwin->get_clock()) {
			char       tbuf[32];
			const int  h   = clk->get_hour();
			std::snprintf(tbuf, sizeof tbuf, "Day %d", clk->get_day() + 1);
			text(5, tbuf, sx, PAD_Y + 4);
			std::snprintf(tbuf, sizeof tbuf, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, clk->get_minute(), h < 12 ? "AM" : "PM");
			text(5, tbuf, sx, PAD_Y + 14);
		}
		if (zoom_on) {
			buf->fill8(C_BLUE, sw, 11, sx - 1, PAD_Y + 26);
			text(5, "ZOOM x2 (L)", sx + 1, PAD_Y + 28);
		} else if (gwin->in_combat()) {
			buf->fill8(C_RED, sw, 11, sx - 1, PAD_Y + 26);
			text(5, Combat::is_paused() ? "COMBAT-PAUSED" : "COMBAT", sx + 1, PAD_Y + 28);
		}
		{
			Gump_manager* gm = gwin->get_gump_man();
			Mouse*        m  = Mouse::mouse();
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
				const std::string n = obj->get_name();
				int               y = text_wrapped(0, n, sx, PAD_Y + 42, sw, 3, 11);
				if (Actor* a = obj->as_actor()) {    // people: show their health
					if (!a->is_dead()) {
						bar(sx, y + 2, sw, a->get_effective_prop(Actor::health), a->get_effective_prop(Actor::strength), C_RED);
					}
				} else if (obj->get_quantity() > 1) {
					char qbuf[16];
					std::snprintf(qbuf, sizeof qbuf, "x%d", obj->get_quantity());
					text(5, qbuf, sx, y + 1);
				}
			}
		}
		const int bw = (sw - 4) / 2, by = PAD_Y + PAD_H - 28;
		panel(sx - 1, by, bw, 24, pressed_id == 297);
		text_centered(0, "?", sx - 1 + bw / 2, by + 6);
		hits.push_back({sx - 2, by - 2, bw + 3, 28, SDLK_UNKNOWN, SDL_KMOD_NONE, 297});
		panel(sx + bw + 3, by, bw, 24, minimap_on || pressed_id == 296);
		text_centered(5, "map", sx + bw + 3 + bw / 2, by + 8);
		hits.push_back({sx + bw + 2, by - 2, bw + 4, 28, SDLK_UNKNOWN, SDL_KMOD_NONE, 296});

		if (minimap_on) {
			draw_minimap(gwin);
			hits.push_back({0, PAD_Y, MM_W, PAD_H, SDLK_UNKNOWN, SDL_KMOD_NONE, 295});    // tap = walk there
			return;
		}
		// Live picture of the top screen (drawn into the framebuffer by
		// blit_view); touching it points and clicks at that spot.
		view_visible = true;
	}

	void draw_bar(Game_window* gwin) {
		buf->fill8(C_BG, W, BAR_H, 0, BAR_Y);
		struct B {
			int         icon;
			const char* label;
			SDL_Keycode key;
			SDL_Keymod  mod;
		};
		static const B row[10] = {
				{SB_MAPS, "Map", SDLK_M, SDL_KMOD_NONE},          {SB_SPELLBOOK, "Spells", SDLK_B, SDL_KMOD_NONE},
				{SB_NOTEBOOK, "Notes", SDLK_N, SDL_KMOD_NONE},    {SB_KEYRING, "Keys", SDLK_K, SDL_KMOD_NONE},
				{SB_BACKPACK, "Pack", SDLK_I, SDL_KMOD_NONE},     {SB_COMBAT, "Combat", SDLK_C, SDL_KMOD_NONE},
				{SB_TARGET, "Target", SDLK_T, SDL_KMOD_NONE},     {SB_FOOD, "Food", SDLK_F, SDL_KMOD_NONE},
				{SB_DISK, "Save", SDLK_S, SDL_KMOD_NONE},         {-1, "Menu", SDLK_ESCAPE, SDL_KMOD_NONE},
		};
		const int bw = W / 5, bh = BAR_H / 2;
		for (int i = 0; i < 10; i++) {
			const int  bx = (i % 5) * bw, by = BAR_Y + (i / 5) * bh;
			const int  id = 300 + i;
			const bool on = (i == 5 && gwin->in_combat());
			panel(bx + 1, by + 1, bw - 2, bh - 2, on || pressed_id == id);
			if (row[i].icon >= 0) {
				Shape_frame* ic = sb_icon(row[i].icon);
				const int    iw = ic ? std::min(ic->get_width(), 30) : 0;
				buf->set_clip(bx + 2, by + 2, iw + 2, bh - 4);
				paint_centered(ic, bx + 3 + iw / 2, by + bh / 2);
				buf->clear_clip();
				text(5, row[i].label, bx + iw + 6, by + bh / 2 - 4);
			} else {
				text_centered(5, row[i].label, bx + bw / 2, by + bh / 2 - 4);
			}
			hits.push_back({bx, by, bw, bh, row[i].key, row[i].mod, id});
		}
	}

	void push_key(SDL_Keycode key, SDL_Keymod mod) {
		SDL_Event e{};
		e.type      = SDL_EVENT_KEY_DOWN;
		e.key.key   = key;
		e.key.mod   = mod;
		e.key.down  = true;
		e.key.scancode = SDL_SCANCODE_UNKNOWN;
		SDL_PushEvent(&e);
		e.type     = SDL_EVENT_KEY_UP;
		e.key.down = false;
		SDL_PushEvent(&e);
	}

	int hit_at(int x, int y) {
		for (size_t i = 0; i < hits.size(); i++) {
			const Hit& h = hits[i];
			if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}
	// ---- D-pad target cycling -------------------------------------------
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
				continue;    // off screen
			}
			const int dx = cx - w / 2, dy = cy - h / 2;
			// People first, then containers, then other things.
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
		N3DS_SetCursor(static_cast<float>(list[idx].cx), static_cast<float>(list[idx].cy));
		last_draw = 0;    // show its name on the bottom screen right away
	}
}    // namespace

// Called by SDL's 3DS backend with every finished top-screen frame (the
// window surface, RGBA8888, game pixels 1:1). Keeps a ~15 fps, 2x2-filtered
// RGB565 copy for the bottom screen's live view.
extern "C" void N3DS_TopFrame(const void* src, int sw, int sh, int pitch, int bpp) {
	if (!view_visible || bpp != 4 || sw < 2 || sh < 2) {
		return;
	}
	const uint64_t now = SDL_GetTicks();
	if (now - view_last < 66) {
		return;
	}
	view_last = now;
	const auto* base = static_cast<const uint8_t*>(src);
	if (zoom_on) {    // 2x around the frozen zoom point: plain upscale
		for (int ty = 0; ty < PAD_H; ty++) {
			const int       sy  = std::min(zoom_y0 + ty * ZOOM_H / PAD_H, sh - 1);
			const uint32_t* r0  = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(sy) * pitch);
			uint16_t*       out = view_px + ty * VIEW_W;
			for (int tx = 0; tx < VIEW_W; tx++) {
				const uint32_t a = r0[std::min(zoom_x0 + tx * ZOOM_W / VIEW_W, sw - 1)];
				out[tx] = static_cast<uint16_t>(((a >> 27) << 11) | (((a >> 18) & 63) << 5) | ((a >> 11) & 31));
			}
		}
		view_new = true;
		return;
	}
	for (int ty = 0; ty < PAD_H; ty++) {
		const int       sy = std::min(ty * sh / PAD_H, sh - 2);
		const uint32_t* r0 = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(sy) * pitch);
		const uint32_t* r1 = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(sy + 1) * pitch);
		uint16_t*       out = view_px + ty * VIEW_W;
		for (int tx = 0; tx < VIEW_W; tx++) {
			const int      sx = std::min(tx * sw / VIEW_W, sw - 2);
			const uint32_t a = r0[sx], b = r0[sx + 1], c = r1[sx], d = r1[sx + 1];
			// RGBA8888: R in bits 24-31, G 16-23, B 8-15
			const unsigned rr = ((a >> 24) + (b >> 24) + (c >> 24) + (d >> 24)) >> 2;
			const unsigned gg = (((a >> 16) & 255) + ((b >> 16) & 255) + ((c >> 16) & 255) + ((d >> 16) & 255)) >> 2;
			const unsigned bb = (((a >> 8) & 255) + ((b >> 8) & 255) + ((c >> 8) & 255) + ((d >> 8) & 255)) >> 2;
			out[tx]           = static_cast<uint16_t>(((rr >> 3) << 11) | ((gg >> 2) << 5) | (bb >> 3));
		}
	}
	view_new = true;
}

extern "C" void N3DS_CycleTarget(int dir) {    // from SDL's input layer
	pending_cycle += dir;
}

void n3ds_bottom_init() {
	// Bottom screen as a single-buffered RGB565 framebuffer (the libctru
	// console uses the same setup, so the pre-game legend keeps working).
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);
}

void n3ds_bottom_tick();

// Called by SDL's input polling: keeps the bottom screen alive while the game
// is paused (an open inventory pauses Ultima VII, so no frames are drawn).
extern "C" void N3DS_InputTick() {
	static bool busy = false;    // never re-enter (the tick can pump events)
	if (!busy) {
		busy = true;
		n3ds_bottom_tick();
		busy = false;
	}
}

// Called after every frame is presented on the top screen.
void n3ds_bottom_tick() {
	n3ds::stereo_tick();
	Game_window* gwin = Game_window::get_instance();
	if (!gwin || !gwin->get_main_actor() || !gwin->get_pal() || !Shape_manager::get_instance()) {
		return;    // still in Exult's menus: the console legend stays up
	}
	const uint64_t now = SDL_GetTicks();
	{
		static uint64_t last_flush = 0;
		if (now - last_flush > 2000) {
			n3ds_log_flush();
			last_flush = now;
		}
	}
	if (pending_cycle != 0) {
		const int dir = pending_cycle > 0 ? 1 : -1;
		pending_cycle = 0;
		do_cycle(gwin, dir);
	}
	// Zoom: L held freezes a 2x window around the cursor.
	{
		const bool l_held = (hidKeysHeld() & KEY_L) != 0;
		if (l_held && !zoom_on) {
			Mouse*    m  = Mouse::mouse();
			const int cx = m ? m->get_mousex() : 200, cy = m ? m->get_mousey() : 120;
			zoom_x0      = std::clamp(cx - ZOOM_W / 2, 0, gwin->get_width() - ZOOM_W);
			zoom_y0      = std::clamp(cy - ZOOM_H / 2, 0, gwin->get_height() - ZOOM_H);
			zoom_on      = true;
			view_last    = 0;
			last_draw    = 0;
		} else if (!l_held && zoom_on) {
			zoom_on   = false;
			view_last = 0;
			last_draw = 0;
		}
	}
	// Item dragged onto a portrait: open that member's inventory and park
	// the cursor on it.
	if (drop_member >= 0) {
		if (!gwin->is_dragging()) {
			drop_member = -1;
		} else {
			Actor* a = drop_member == 0 ? gwin->get_main_actor() : nullptr;
			if (drop_member > 0) {
				Party_manager* pm = gwin->get_party_man();
				if (pm && drop_member - 1 < pm->get_count()) {
					a = gwin->get_npc(pm->get_member(drop_member - 1));
				}
			}
			Gump_manager* gm = gwin->get_gump_man();
			if (a && gm) {
				const int shp = a->inventory_shapenum();
				Gump*     g   = gm->find_gump(a, shp);
				if (!g) {
					gm->add_gump(a, shp, true);
					g = gm->find_gump(a, shp);
				}
				if (g) {
					const TileRect r = g->get_rect();
					N3DS_SetCursor(static_cast<float>(r.x + r.w / 2), static_cast<float>(r.y + r.h / 2));
				}
			}
		}
	}
	if (pending_tx >= 0) {
		travel_to(gwin, pending_tx, pending_ty);
		pending_tx = pending_ty = -1;
		last_draw               = 0;
	}
	if (mode == GAME && now - last_draw < 100) {
		if (view_visible && view_new) {    // refresh just the live view between UI redraws
			auto* fb = reinterpret_cast<uint16_t*>(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr));
			blit_view(fb);
			GSPGPU_FlushDataCache(fb, W * H * 2);
		}
		return;
	}
	last_draw = now;
	if (!buf) {
		buf = new Image_buffer8(W, H);
	}
	Palette* pal = gwin->get_pal();
	if (!day_pal_cached || mode == LEGEND) {
		// Use the daylight palette for the UI so it stays readable at night.
		load_palette(gwin);
		day_pal_cached = pal->get_palette_index() == 0 && pal->get_brightness() == 100;
	}
	mode = GAME;

	Conversation* conv = nullptr;
	if (Usecode_machine* uc = gwin->get_usecode()) {
		conv = uc->get_conversation();
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
			push_key(SDLK_S, SDL_KMOD_LCTRL);    // Exult's Ctrl-S = quicksave
			next_autosave = now + N3DS_AUTOSAVE_MS;
			std::printf("[exult-3ds] autosave\n");
		}
	}

	hits.clear();
	// Exult's font code always paints into the shared render target, so point
	// it at our buffer while drawing and restore it afterwards.
	Image_buffer8* prev = Shape_frame::get_render_target();
	Shape_frame::set_to_render(buf);
	draw_party(gwin);
	draw_pad(gwin, conv);
	draw_bar(gwin);
	Shape_frame::set_to_render(prev);
	blit();
}

// Touch hook called from SDL's N3DS input layer (main thread).
// phase: 0 = down, 1 = move, 2 = up.
// Returns 1 if the bottom-screen UI took the touch (no mouse emulation),
// or 0 with nx/ny = normalised touchpad position for the top-screen cursor.
extern "C" int N3DS_BottomTouch(int phase, int x, int y, float* nx, float* ny) {
	if (mode != GAME) {
		*nx = x / 320.0f;
		*ny = y / 240.0f;
		return 0;
	}
	if (phase == 0) {
		touch_hit = hit_at(x, y);
		touch_ui  = touch_hit >= 0 || y < PAD_Y || y >= BAR_Y || (x >= SIDE_X && !show_help && !choices_shown);
		pressed_id = touch_hit >= 0 ? hits[touch_hit].id : -1;
		last_draw  = 0;    // redraw promptly to show the press
	}
	if (touch_ui) {
		if (phase == 2) {
			const int now_hit = hit_at(x, y);
			if (touch_hit >= 0 && now_hit == touch_hit) {
				const Hit h = hits[touch_hit];
				if (h.id == 297) {
					show_help = true;
				} else if (h.id == 296) {
					minimap_on = !minimap_on;
					mm_built   = 0;
				} else if (h.id == 295) {
					pending_tx = (mm_ox + (x - 1) / MM_SCALE + c_num_tiles) % c_num_tiles;
					pending_ty = (mm_oy + (y - PAD_Y - 1) / MM_SCALE + c_num_tiles) % c_num_tiles;
				} else if (h.id == 298) {
					show_help = false;
				} else if (h.id == 299) {
					answer_page++;
				} else if (h.key != SDLK_UNKNOWN) {
					push_key(h.key, h.mod);
				}
			}
			pressed_id = -1;
			touch_hit  = -1;
			touch_ui   = false;
			last_draw  = 0;
		}
		return 1;
	}
	// A drag that started in the live view and is now over a portrait.
	if (phase == 1 && y < PAD_Y) {
		Game_window* gwin = Game_window::get_instance();
		const int    slot = x / (W / 8);
		if (gwin && gwin->is_dragging() && slot < party_count) {
			if (drop_member != slot) {
				drop_member = slot;
				last_draw   = 0;
			}
			return 2;    // keep the button down; the tick steers the cursor
		}
	}
	if (phase == 2 && drop_member >= 0) {
		drop_member = -1;    // released over the portrait: drop where the cursor is
		last_draw   = 0;
		return 2;
	}
	if (drop_member >= 0) {    // dragged back into the view
		drop_member = -1;
		last_draw   = 0;
	}
	float fx = x / static_cast<float>(VIEW_W), fy = (y - PAD_Y) / static_cast<float>(PAD_H);
	fx       = fx < 0 ? 0 : fx > 1 ? 1 : fx;
	fy       = fy < 0 ? 0 : fy > 1 ? 1 : fy;
	if (zoom_on) {
		Game_window* gwin = Game_window::get_instance();
		const float  gw = gwin ? gwin->get_width() : 400.0f, gh = gwin ? gwin->get_height() : 240.0f;
		fx              = (zoom_x0 + fx * ZOOM_W) / gw;
		fy              = (zoom_y0 + fy * ZOOM_H) / gh;
	}
	*nx = fx;
	*ny = fy;
	return 0;
}
