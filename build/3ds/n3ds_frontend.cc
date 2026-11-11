// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 the exult-3ds authors
/*
 * Exult for New Nintendo 3DS - start screen.
 *
 * Shown right after launch, before Exult loads anything, and drawn by the port
 * itself (its own copy of the U7 Pixel Sans font, straight into the
 * framebuffers), so it appears at once. On the touchscreen:
 *   1. pick a game: The Black Gate or Serpent Isle (only those installed);
 *   2. Continue (the game in progress), New Game (name, male / female, skin)
 *      or Load Game (saves by name and date);
 * then Exult starts that directly: no game picker, title screen, loading
 * animation or character screen. "Exult's own menus" falls back to the
 * normal flow (intro, credits, mods).
 *
 * The choice is handed to Exult through n3ds_frontend_game_choice() (in
 * exult.cc) and n3ds_frontend_take() (in Game::show_menu, game.cc).
 *
 * GPL v2 (same as Exult).
 */
#include <3ds.h>

#include "n3ds_font.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {
	constexpr int TW = 400, BW = 320, H = 240;
	constexpr const char* HOME_DIR = "sdmc:/3ds/exult";

	uint32_t rgb(int r, int g, int b) {
		return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | 0xFF;
	}

	const uint32_t C_BG = rgb(20, 15, 10), C_PANEL = rgb(70, 50, 30), C_PANEL_HI = rgb(150, 110, 50), C_EDGE = rgb(120, 90, 50),
				   C_TEXT = rgb(236, 236, 236), C_DIM = rgb(165, 155, 135), C_GOLD = rgb(232, 192, 96), C_OFF = rgb(45, 36, 26),
				   C_RED = rgb(200, 60, 50);

	// ---- Drawing ------------------------------------------------------------
	struct Canvas {
		int                   w;
		std::vector<uint32_t> px;

		explicit Canvas(int width) : w(width), px(static_cast<size_t>(width) * H, C_BG) {}

		void fill(int x, int y, int fw, int fh, uint32_t c) {
			const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + fw), y1 = std::min(H, y + fh);
			for (int yy = y0; yy < y1; yy++) {
				std::fill(&px[static_cast<size_t>(yy) * w + x0], &px[static_cast<size_t>(yy) * w + x1], c);
			}
		}

		void glyph(int ch, int x, int y, int s, uint32_t c) {
			if (ch < 32 || ch > 126) {
				ch = '?';
			}
			const N3dsGlyph& g = n3ds_glyphs[ch - 32];
			for (int r = 0; r < g.h; r++) {
				for (int col = 0; col < g.w; col++) {
					if (g.rows[r] & (1u << col)) {
						fill(x + col * s, y + r * s, s, s, c);
					}
				}
			}
		}

		static int text_w(const std::string& t, int s) {
			int n = 0;
			for (unsigned char ch : t) {
				n += (ch >= 32 && ch <= 126 ? n3ds_glyphs[ch - 32].w : 3) + 1;
			}
			return n > 0 ? (n - 1) * s : 0;
		}

		void text(const std::string& t, int x, int y, int s, uint32_t c) {
			for (unsigned char ch : t) {
				glyph(ch, x, y, s, c);
				x += ((ch >= 32 && ch <= 126 ? n3ds_glyphs[ch - 32].w : 3) + 1) * s;
			}
		}

		void text_c(const std::string& t, int cx, int y, int s, uint32_t c) {
			text(t, cx - text_w(t, s) / 2, y, s, c);
		}

		// Fit into maxw by trimming (with "...").
		void text_fit(std::string t, int x, int y, int s, uint32_t c, int maxw) {
			if (text_w(t, s) > maxw) {
				while (t.size() > 1 && text_w(t + "...", s) > maxw) {
					t.pop_back();
				}
				t += "...";
			}
			text(t, x, y, s, c);
		}

		void present(gfxScreen_t screen) const {
			u16   fw = 0, fh = 0;
			auto* fb = reinterpret_cast<uint32_t*>(gfxGetFramebuffer(screen, GFX_LEFT, &fw, &fh));
			// Framebuffers are rotated: fw = 240 rows of fh = 320/400 pixels.
			if (gfxGetScreenFormat(screen) != GSP_RGBA8_OES) {
				return;
			}
			for (int x = 0; x < w && x < fh; x++) {
				uint32_t* col = fb + x * fw;
				for (int y = 0; y < H && y < fw; y++) {
					col[fw - 1 - y] = px[static_cast<size_t>(y) * w + x];
				}
			}
			GSPGPU_FlushDataCache(fb, fw * fh * 4);
			gfxScreenSwapBuffers(screen, false);
		}
	};

	// ---- Data about the installed games and saves ----------------------------
	bool exists(const std::string& p) {
		struct stat st;
		return stat(p.c_str(), &st) == 0;
	}

	std::string when(time_t t) {
		if (t <= 0) {
			return "";
		}
		char       b[64];
		struct tm* tm = localtime(&t);
		if (!tm) {
			return "";
		}
		std::strftime(b, sizeof b, "%b %d, %I:%M %p", tm);
		return b;
	}

	struct Save {
		std::string path, name;
		int         slot;
		time_t      mtime;
	};

	// The save's name: a zip save keeps it as the archive comment, an old
	// flex save in its 80-byte title.
	std::string save_name(const std::string& path) {
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) {
			return "";
		}
		char head[80] = {};
		std::fread(head, 1, sizeof head, f);
		std::string name;
		if (head[0] == 'P' && head[1] == 'K') {
			std::fseek(f, 0, SEEK_END);
			const long size = std::ftell(f);
			const long n    = std::min(size, 2048L);
			std::vector<unsigned char> tail(static_cast<size_t>(n));
			std::fseek(f, size - n, SEEK_SET);
			std::fread(tail.data(), 1, tail.size(), f);
			for (long i = n - 22; i >= 0; i--) {
				if (tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6) {
					const int len = tail[i + 20] | (tail[i + 21] << 8);
					if (i + 22 + len <= n) {
						name.assign(reinterpret_cast<char*>(&tail[i + 22]), len);
					}
					break;
				}
			}
		} else {
			name.assign(head, strnlen(head, sizeof head));
		}
		std::fclose(f);
		std::string clean;
		for (char c : name) {
			if (c >= 32 && c <= 126) {
				clean += c;
			}
		}
		return clean;
	}

	std::vector<Save> list_saves(const char* tag) {
		std::vector<Save> out;
		const std::string dir = std::string(HOME_DIR) + "/saves/" + tag;
		if (DIR* d = opendir(dir.c_str())) {
			while (dirent* e = readdir(d)) {
				const std::string n = e->d_name;
				int               slot = -1;
				char              t[8] = {};
				if (std::sscanf(n.c_str(), "exult%d%2[a-z].sav", &slot, t) == 2 && std::string(t) == tag) {
					Save s;
					s.path = dir + "/" + n;
					s.slot = slot;
					struct stat st;
					s.mtime = stat(s.path.c_str(), &st) == 0 ? st.st_mtime : 0;
					s.name  = save_name(s.path);
					if (s.name.empty()) {
						s.name = "Save " + std::to_string(slot);
					}
					out.push_back(s);
				}
			}
			closedir(d);
		}
		std::sort(out.begin(), out.end(), [](const Save& a, const Save& b) {
			return a.mtime > b.mtime;
		});
		return out;
	}

	struct GameInfo {
		const char* dir;     // under sdmc:/3ds/exult
		const char* tag;     // save-file suffix
		const char* title;
		const char* sub;
		bool        installed = false;
		bool        in_progress = false;
		time_t      last = 0;
	};

	GameInfo games[2] = {
			{"blackgate", "bg", "The Black Gate", "with Forge of Virtue"},
			{"serpentisle", "si", "Serpent Isle", "with The Silver Seed"},
	};

	void scan_games() {
		for (auto& g : games) {
			const std::string base = std::string(HOME_DIR) + "/" + g.dir;
			g.installed            = exists(base + "/static/u7chunks") || exists(base + "/STATIC/U7CHUNKS");
			struct stat st;
			g.in_progress = stat((base + "/gamedat/identity").c_str(), &st) == 0;
			g.last        = 0;
			if (g.in_progress) {
				struct stat npc;
				g.last = stat((base + "/gamedat/npc.dat").c_str(), &npc) == 0 ? npc.st_mtime : st.st_mtime;
			}
		}
	}

	// ---- The choice -------------------------------------------------------------
	int         chosen_game   = 0;    // 1 BG, 2 SI, 0 Exult's own menus
	int         chosen_action = 0;    // 1 continue, 2 new game, 3 load
	std::string chosen_name   = "Avatar";
	int         chosen_female = 0, chosen_skin = 0;
	std::string chosen_save;
	bool        taken = false;

	// ---- Buttons ----------------------------------------------------------------
	struct Button {
		int         x, y, w, h;
		std::string label, sub;
		int         id;
		bool        enabled = true, on = false;
	};

	void draw_button(Canvas& c, const Button& b, bool focus, bool pressed) {
		const uint32_t edge = focus ? C_GOLD : C_EDGE;
		c.fill(b.x, b.y, b.w, b.h, edge);
		const uint32_t face = !b.enabled ? C_OFF : (pressed || b.on) ? C_PANEL_HI : C_PANEL;
		c.fill(b.x + 2, b.y + 2, b.w - 4, b.h - 4, face);
		const uint32_t tc = b.enabled ? C_TEXT : C_DIM;
		if (b.sub.empty()) {
			c.text_c(b.label, b.x + b.w / 2, b.y + (b.h - 9) / 2, 1, tc);
		} else {
			c.text_c(b.label, b.x + b.w / 2, b.y + b.h / 2 - 12, 1, tc);
			c.text_c(b.sub, b.x + b.w / 2, b.y + b.h / 2 + 3, 1, C_DIM);
		}
	}

	enum Screen { PICK, ACTIONS, NEWGAME, LOAD };

	void draw_top(Canvas& t, Screen s, int g, const std::vector<std::string>& lines) {
		std::fill(t.px.begin(), t.px.end(), C_BG);
		t.text_c("ULTIMA VII", TW / 2, 22, 3, C_GOLD);
		t.text_c(g > 0 ? games[g - 1].title : "for the New Nintendo 3DS", TW / 2, 60, g > 0 ? 2 : 1, C_TEXT);
		t.fill(60, 88, TW - 120, 1, C_EDGE);
		int y = 100;
		for (const auto& l : lines) {
			t.text_c(l, TW / 2, y, 1, l.rfind("!", 0) == 0 ? C_RED : C_DIM);
			y += 15;
		}
		(void)s;
		t.fill(0, H - 16, TW, 16, C_PANEL);
		t.text_c("Tap a button, or use the D-Pad and A.  B: back", TW / 2, H - 13, 1, C_DIM);
	}

	bool keyboard(std::string& name) {
		SwkbdState kb;
		char       buf[32] = {};
		swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 16);
		swkbdSetHintText(&kb, "The Avatar's name");
		swkbdSetInitialText(&kb, name.c_str());
		swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
		const SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
		if (b == SWKBD_BUTTON_CONFIRM && buf[0]) {
			name = buf;
			return true;
		}
		return false;
	}
}    // namespace

// Runs the start screen; returns when a choice is made.
extern "C" void n3ds_frontend_run() {
	scan_games();
	int installed = 0, only = 0;
	for (int i = 0; i < 2; i++) {
		if (games[i].installed) {
			installed++;
			only = i + 1;
		}
	}
	if (installed == 0) {
		chosen_game = 0;    // nothing found: let Exult explain
		return;
	}
	gfxSetScreenFormat(GFX_TOP, GSP_RGBA8_OES);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetScreenFormat(GFX_BOTTOM, GSP_RGBA8_OES);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);

	Canvas            top(TW), bot(BW);
	Screen            screen = installed == 1 ? ACTIONS : PICK;
	int               g      = installed == 1 ? only : 0;
	std::vector<Save> saves;
	int               load_page = 0, focus = 0, pressed = -1;
	bool              touching  = false;
	if (g) {
		saves = list_saves(games[g - 1].tag);
	}

	for (;;) {
		if (!aptMainLoop()) {
			exit(0);
		}
		// Build this screen's buttons.
		std::vector<Button>      b;
		std::vector<std::string> info;
		switch (screen) {
		case PICK:
			for (int i = 0; i < 2; i++) {
				Button k{10, 14 + i * 76, BW - 20, 66, games[i].title, games[i].installed ? games[i].sub : "not installed", i + 1};
				k.enabled = games[i].installed;
				b.push_back(k);
			}
			b.push_back({10, 176, BW - 20, 32, "Exult's own menus (intro, credits, mods)", "", 9});
			info = {"Pick a game on the touchscreen.", "", "Your games are read from the SD card:",
					"sdmc:/3ds/exult/blackgate and /serpentisle"};
			break;
		case ACTIONS: {
			const GameInfo& gi = games[g - 1];
			Button          cont{10, 10, BW - 20, 58, "Continue", gi.in_progress ? (when(gi.last).empty() ? std::string("Game in progress") : "Last played " + when(gi.last))
							  : std::string("No game in progress"), 1};
			cont.enabled = gi.in_progress;
			b.push_back(cont);
			b.push_back({10, 74, BW - 20, 48, "New Game", "Name your Avatar and start in Trinsic", 2});
			Button ld{10, 128, BW - 20, 48, "Load Game", saves.empty() ? "No saves yet" : std::to_string(saves.size()) + " saved games", 3};
			ld.enabled = !saves.empty();
			b.push_back(ld);
			if (installed > 1) {
				b.push_back({10, 184, BW - 20, 32, "Back", "", 0});
			}
			info = {gi.in_progress ? "Continue picks up the game in progress." : "Start a new game, or load a saved one.",
					"New Game starts in Trinsic, as the Avatar arrives.", "", "In the game, L+Y opens the manual."};
			break;
		}
		case NEWGAME: {
			b.push_back({10, 8, BW - 20, 40, "Name: " + chosen_name, "tap to change", 1});
			Button m{10, 56, (BW - 30) / 2, 34, "Male", "", 2}, f{20 + (BW - 30) / 2, 56, (BW - 30) / 2, 34, "Female", "", 3};
			m.on = !chosen_female;
			f.on = chosen_female != 0;
			b.push_back(m);
			b.push_back(f);
			static const char* skins[3] = {"Light", "Medium", "Dark"};
			for (int i = 0; i < 3; i++) {
				const int bw = (BW - 40) / 3;
				Button    s{10 + i * (bw + 10), 98, bw, 34, skins[i], "", 4 + i};
				s.on = chosen_skin == i;
				b.push_back(s);
			}
			b.push_back({10, 142, BW - 20, 50, "Start the game", "", 10});
			b.push_back({10, 200, BW - 20, 32, "Back", "", 0});
			info = {"Name: " + chosen_name, std::string(chosen_female ? "Female" : "Male") + ", " + skins[chosen_skin] + " skin", "",
					"The skin choice applies where the game", "has portraits for it."};
			break;
		}
		case LOAD: {
			constexpr int per = 5;
			const int     pages = std::max(1, static_cast<int>((saves.size() + per - 1) / per));
			load_page           = std::clamp(load_page, 0, pages - 1);
			for (int k = 0; k < per && load_page * per + k < static_cast<int>(saves.size()); k++) {
				const Save& s = saves[load_page * per + k];
				b.push_back({10, 6 + k * 38, BW - 20, 34, s.name, when(s.mtime), 100 + load_page * per + k});
			}
			const int by = 200;
			if (pages > 1) {
				Button prev{10, by, 70, 32, "< Prev", "", 50}, next{BW - 80, by, 70, 32, "Next >", "", 51};
				prev.enabled = load_page > 0;
				next.enabled = load_page < pages - 1;
				b.push_back(prev);
				b.push_back(next);
			}
			b.push_back({90, by, BW - 180, 32, "Back", "", 0});
			info = {"Pick a saved game.", "Page " + std::to_string(load_page + 1) + " of " + std::to_string(pages)};
			break;
		}
		}
		focus = std::clamp(focus, 0, static_cast<int>(b.size()) - 1);
		while (!b[focus].enabled && focus + 1 < static_cast<int>(b.size())) {
			focus++;
		}

		// Input.
		hidScanInput();
		const u32 down = hidKeysDown(), held = hidKeysHeld();
		int       activate = -1;
		touchPosition tp;
		hidTouchRead(&tp);
		auto hit = [&](int x, int y) {
			for (size_t i = 0; i < b.size(); i++) {
				if (b[i].enabled && x >= b[i].x && x < b[i].x + b[i].w && y >= b[i].y && y < b[i].y + b[i].h) {
					return static_cast<int>(i);
				}
			}
			return -1;
		};
		if (held & KEY_TOUCH) {
			if (!touching) {
				pressed  = hit(tp.px, tp.py);
				touching = true;
			}
		} else if (touching) {
			touching = false;
			static touchPosition last;
			(void)last;
			if (pressed >= 0) {
				activate = pressed;    // released on a button
			}
			pressed = -1;
		}
		if (down & (KEY_DUP | KEY_DLEFT)) {
			for (int i = focus - 1; i >= 0; i--) {
				if (b[i].enabled) {
					focus = i;
					break;
				}
			}
		}
		if (down & (KEY_DDOWN | KEY_DRIGHT)) {
			for (int i = focus + 1; i < static_cast<int>(b.size()); i++) {
				if (b[i].enabled) {
					focus = i;
					break;
				}
			}
		}
		if (down & KEY_A) {
			activate = focus;
		}
		int id = activate >= 0 ? b[activate].id : -1;
		if ((down & KEY_B) && screen != PICK && !(screen == ACTIONS && installed == 1)) {
			id = 0;
		}

		// Act.
		if (id >= 0) {
			switch (screen) {
			case PICK:
				if (id == 9) {
					chosen_game = 0;
					return;
				}
				g      = id;
				saves  = list_saves(games[g - 1].tag);
				screen = ACTIONS;
				focus  = games[g - 1].in_progress ? 0 : 1;
				break;
			case ACTIONS:
				if (id == 0) {
					screen = PICK;
					focus  = g - 1;
				} else if (id == 1) {
					chosen_game = g, chosen_action = 1;
				} else if (id == 2) {
					screen = NEWGAME;
					focus  = 6;    // Start the game
				} else if (id == 3) {
					screen    = LOAD;
					load_page = 0;
					focus     = 0;
				}
				break;
			case NEWGAME:
				if (id == 0) {
					screen = ACTIONS;
					focus  = 1;
				} else if (id == 1) {
					keyboard(chosen_name);
				} else if (id == 2 || id == 3) {
					chosen_female = id == 3;
				} else if (id >= 4 && id <= 6) {
					chosen_skin = id - 4;
				} else if (id == 10) {
					chosen_game = g, chosen_action = 2;
				}
				break;
			case LOAD:
				if (id == 0) {
					screen = ACTIONS;
					focus  = 2;
				} else if (id == 50) {
					load_page--;
				} else if (id == 51) {
					load_page++;
				} else if (id >= 100) {
					chosen_save = saves[id - 100].path;
					chosen_game = g, chosen_action = 3;
				}
				break;
			}
			if (chosen_action) {
				// A last frame while Exult loads.
				std::fill(bot.px.begin(), bot.px.end(), C_BG);
				bot.text_c("Loading", BW / 2, 96, 2, C_GOLD);
				bot.text_c(games[g - 1].title, BW / 2, 126, 1, C_TEXT);
				bot.text_c("This can take a minute.", BW / 2, 150, 1, C_DIM);
				bot.present(GFX_BOTTOM);
				return;
			}
			continue;
		}

		// Draw.
		std::fill(bot.px.begin(), bot.px.end(), C_BG);
		for (size_t i = 0; i < b.size(); i++) {
			draw_button(bot, b[i], static_cast<int>(i) == focus, static_cast<int>(i) == pressed);
		}
		draw_top(top, screen, g, info);
		top.present(GFX_TOP);
		bot.present(GFX_BOTTOM);
		gspWaitForVBlank();
	}
}

// exult.cc: which game to start (1 BG, 2 SI), once; 0 = Exult's own menus.
extern "C" int n3ds_frontend_game_choice() {
	const int g = chosen_game;
	chosen_game = 0;
	return g;
}

// game.cc (Game::show_menu): how to start it, once.
extern "C" bool n3ds_frontend_take(int* action, const char** name, int* female, int* skin, const char** save) {
	if (taken || chosen_action == 0) {
		return false;
	}
	taken   = true;
	*action = chosen_action;
	*name   = chosen_name.c_str();
	*female = chosen_female;
	*skin   = chosen_skin;
	*save   = chosen_save.c_str();
	return true;
}
