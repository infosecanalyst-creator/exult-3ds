/*
 * Exult for New Nintendo 3DS — platform glue.
 *
 *  - Launched with `3dslink -s exult.3dsx` from the PC (Homebrew Launcher in
 *    netloader mode, press Y): stdout/stderr stream to the PC terminal
 *    instead of the log file, so you can watch Exult run live.
 *  - Runs before main(): enables the New 3DS 804 MHz clock + L2 cache, sets
 *    HOME to sdmc:/3ds/exult (Exult keeps .exult.cfg, saves and gamedat
 *    there), writes a 3DS-tuned default config on first run, and redirects
 *    stdout/stderr to sdmc:/3ds/exult/exult.log.
 *  - n3ds_after_sdl_init(): puts a controls legend on the bottom screen.
 *  - glob()/globfree(): newlib on 3DS has the header but no implementation.
 *
 *  GPL v2 (same as Exult).
 */
#include <3ds.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <malloc.h>
#include <netinet/in.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

#include <exception>
#include <new>
#include <string>
#include <typeinfo>
#include <vector>
#include <cxxabi.h>

#define N3DS_HOME "sdmc:/3ds/exult"

// Heap layout. libctru's default gives the normal heap only 24 MB, which is
// too little for Exult. We take everything the system grants (about 118 MB
// on a New 3DS), keep at least 16 MB of linear memory for SDL/GPU/audio,
// and give the rest to the normal heap - capped at the 96 MB heap window
// (0x08000000-0x0E000000); asking for more than that crashes at launch.
extern "C" {
#include <3ds/allocator/mappable.h>
extern char* fake_heap_start;
extern char* fake_heap_end;
extern u32   __ctru_heap;
extern u32   __ctru_heap_size;
extern u32   __ctru_linear_heap;
extern u32   __ctru_linear_heap_size;

void __system_allocateHeaps(void) {
	Handle reslimit = 0;
	if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) {
		svcBreak(USERBREAK_PANIC);
	}
	s64               maxCommit = 0, currentCommit = 0;
	ResourceLimitType type      = RESLIMIT_COMMIT;
	svcGetResourceLimitLimitValues(&maxCommit, reslimit, &type, 1);
	svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &type, 1);
	svcCloseHandle(reslimit);

	const u32 remaining  = static_cast<u32>(maxCommit - currentCommit) & ~0xFFFu;
	const u32 heap_max   = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;    // 96 MB
	const u32 linear_min = 16u << 20;
	u32       heap       = remaining > linear_min ? remaining - linear_min : remaining / 2;
	if (heap > heap_max) {
		heap = heap_max;
	}
	heap &= ~0xFFFu;
	__ctru_heap_size        = heap;
	__ctru_linear_heap_size = remaining - heap;

	if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
								  static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE)))) {
		svcBreak(USERBREAK_PANIC);
	}
	if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR,
								  static_cast<MemPerm>(MEMPERM_READ | MEMPERM_WRITE)))) {
		svcBreak(USERBREAK_PANIC);
	}
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	fake_heap_start = reinterpret_cast<char*>(__ctru_heap);
	fake_heap_end   = fake_heap_start + __ctru_heap_size;
}
}

static const char* default_cfg =
		"<config>\n"
		"\t<disk>\n"
		"\t\t<data_path>\n\t\t" N3DS_HOME "/data\n\t\t</data_path>\n"
		"\t\t<game>\n"
		"\t\t\t<blackgate>\n\t\t\t\t<path>\n\t\t\t\t" N3DS_HOME "/blackgate\n\t\t\t\t</path>\n"
		"\t\t\t\t<savegame_path>\n\t\t\t\t" N3DS_HOME "/saves/bg\n\t\t\t\t</savegame_path>\n"
		"\t\t\t\t<keys>\n\t\t\t\t(default)\n\t\t\t\t</keys>\n\t\t\t</blackgate>\n"
		"\t\t\t<serpentisle>\n\t\t\t\t<path>\n\t\t\t\t" N3DS_HOME "/serpentisle\n\t\t\t\t</path>\n"
		"\t\t\t\t<savegame_path>\n\t\t\t\t" N3DS_HOME "/saves/si\n\t\t\t\t</savegame_path>\n"
		"\t\t\t\t<keys>\n\t\t\t\t(default)\n\t\t\t\t</keys>\n\t\t\t</serpentisle>\n"
		"\t\t</game>\n"
		"\t\t<save_compression_level>\n\t\t1\n\t\t</save_compression_level>\n"
		"\t</disk>\n"
		"\t<video>\n"
		"\t\t<display>\n\t\t\t<width>\n\t\t\t400\n\t\t\t</width>\n"
		"\t\t\t<height>\n\t\t\t240\n\t\t\t</height>\n\t\t</display>\n"
		"\t\t<game>\n\t\t\t<width>\n\t\t\t400\n\t\t\t</width>\n"
		"\t\t\t<height>\n\t\t\t240\n\t\t\t</height>\n\t\t</game>\n"
		"\t\t<width>\n\t\t400\n\t\t</width>\n"
		"\t\t<height>\n\t\t240\n\t\t</height>\n"
		"\t\t<scale>\n\t\t1\n\t\t</scale>\n"
		"\t\t<scale_method>\n\t\tpoint\n\t\t</scale_method>\n"
		"\t\t<fill_mode>\n\t\tfit\n\t\t</fill_mode>\n"
		"\t\t<fill_scaler>\n\t\tpoint\n\t\t</fill_scaler>\n"
		"\t\t<fullscreen>\n\t\tyes\n\t\t</fullscreen>\n"
		"\t\t<share_video_settings>\n\t\tyes\n\t\t</share_video_settings>\n"
		"\t\t<fps>\n\t\t5\n\t\t</fps>\n"
		"\t\t<ui>\n\t\t\t<universal>\n\t\t\tyes\n\t\t\t</universal>\n"
		"\t\t\t<width>\n\t\t\t400\n\t\t\t</width>\n"
		"\t\t\t<height>\n\t\t\t240\n\t\t\t</height>\n"
		"\t\t\t<scale_method>\n\t\t\tpoint\n\t\t\t</scale_method>\n\t\t</ui>\n"
		"\t</video>\n"
		"\t<audio>\n"
		"\t\t<enabled>\n\t\tyes\n\t\t</enabled>\n"
		"\t\t<sample_rate>\n\t\t22050\n\t\t</sample_rate>\n"
		"\t\t<stereo>\n\t\tno\n\t\t</stereo>\n"
		"\t\t<midi>\n"
		"\t\t\t<enabled>\n\t\t\tyes\n\t\t\t</enabled>\n"
		"\t\t\t<driver>\n\t\t\tFMOpl\n\t\t\t</driver>\n"
		"\t\t\t<use_oggs>\n\t\t\tno\n\t\t\t</use_oggs>\n"
		"\t\t\t<reverb>\n\t\t\t\t<enabled>\n\t\t\t\tno\n\t\t\t\t</enabled>\n\t\t\t</reverb>\n"
		"\t\t\t<chorus>\n\t\t\t\t<enabled>\n\t\t\t\tno\n\t\t\t\t</enabled>\n\t\t\t</chorus>\n"
		"\t\t</midi>\n"
		"\t\t<speech>\n\t\t\t<enabled>\n\t\t\tyes\n\t\t\t</enabled>\n\t\t</speech>\n"
		"\t</audio>\n"
		"\t<gameplay>\n\t\t<skip_splash>\n\t\tyes\n\t\t</skip_splash>\n\t</gameplay>\n"
		"</config>\n";

static FILE*      log_file = nullptr;
static devoptab_t log_dotab {};

// Each line gets a "[seconds]" stamp so slow spots show up in the log. The
// file is buffered and flushed at most once a second (or on a FATAL line):
// flushing every line costs an SD round trip each time, and Exult logs a
// lot during play (NPC path-finding).
static u64  log_t0         = 0;
static u64  log_last_flush = 0;
static bool log_at_bol     = true;

static ssize_t log_write(struct _reent*, void*, const char* ptr, size_t len) {
	if (log_file) {
		const u64 now = osGetTime();
		for (size_t i = 0; i < len;) {
			if (log_at_bol) {
				fprintf(log_file, "[%6.1f] ", (now - log_t0) / 1000.0);
				log_at_bol = false;
			}
			const char* nl  = static_cast<const char*>(memchr(ptr + i, '\n', len - i));
			const size_t n  = nl ? static_cast<size_t>(nl - (ptr + i)) + 1 : len - i;
			fwrite(ptr + i, 1, n, log_file);
			if (nl) {
				log_at_bol = true;
			}
			i += n;
		}
		if (now - log_last_flush >= 1000 || memmem(ptr, len, "FATAL", 5)) {
			fflush(log_file);
			log_last_flush = now;
		}
	}
	return static_cast<ssize_t>(len);
}

static void install_log() {
	devoptab_list[STD_OUT] = &log_dotab;
	devoptab_list[STD_ERR] = &log_dotab;
}

#define SOC_BUFFER_SIZE 0x100000
static u32* soc_buffer   = nullptr;
static bool linked_to_pc = false;

static void log_memory() {
	printf("[exult-3ds] heap %lu MB, linear %lu MB\n", static_cast<unsigned long>(__ctru_heap_size >> 20),
		   static_cast<unsigned long>(__ctru_linear_heap_size >> 20));
	fflush(stdout);
}

// Heap in use right now (MB), for the log and the bottom-screen debug line.
extern "C" int n3ds_heap_used_mb() {
	struct mallinfo mi = mallinfo();
	return static_cast<int>((mi.arena + mi.hblkhd) >> 20);
}

// Make crashes visible: without these, an uncaught exception or running out
// of memory just ends the program silently (abort -> exit) on the 3DS.
static void n3ds_terminate() {
	const char* what = "unknown";
	std::string type = "?";
	if (auto ep = std::current_exception()) {
		try {
			std::rethrow_exception(ep);
		} catch (const std::exception& e) {
			what = e.what();
			if (auto* t = abi::__cxa_current_exception_type()) type = t->name();
		} catch (...) {
		}
	}
	fprintf(stderr, "\n[exult-3ds] FATAL: uncaught exception %s: %s (heap used %d MB)\n", type.c_str(), what,
			n3ds_heap_used_mb());
	fflush(stderr);
	_exit(1);
}

static void n3ds_out_of_memory() {
	fprintf(stderr, "\n[exult-3ds] FATAL: out of memory (heap used %d MB of %lu)\n", n3ds_heap_used_mb(),
			static_cast<unsigned long>(__ctru_heap_size >> 20));
	fflush(stderr);
	std::set_new_handler(nullptr);
	throw std::bad_alloc();
}

static bool file_exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

// One-time update of a config written by an earlier build: turn on
// hold-to-walk (touch_pathfind) and fix the game speed (fps is Exult's
// animation speed; the first build wrote 30, the options screen could then
// save garbage).
static void migrate_cfg() {
	FILE* f = fopen(N3DS_HOME "/exult.cfg", "rb");
	if (!f) {
		return;
	}
	std::string cfg;
	char        chunk[4096];
	size_t      n;
	while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
		cfg.append(chunk, n);
	}
	fclose(f);
	bool changed = false;
	auto replace = [&](const std::string& from, const std::string& to) {
		const size_t p = cfg.find(from);
		if (p != std::string::npos) {
			cfg.replace(p, from.size(), to);
			changed = true;
		}
	};
	replace("<touch_pathfind>no</touch_pathfind>", "<touch_pathfind>yes</touch_pathfind>");
	const size_t a = cfg.find("<fps>"), b = cfg.find("</fps>");
	if (a != std::string::npos && b != std::string::npos && b > a) {
		const int fps = atoi(cfg.c_str() + a + 5);
		if (fps <= 0 || fps > 20) {
			cfg.replace(a + 5, b - a - 5, "5");
			changed = true;
		}
	}
	if (changed) {
		if (FILE* o = fopen(N3DS_HOME "/exult.cfg", "wb")) {
			fwrite(cfg.data(), 1, cfg.size(), o);
			fclose(o);
		}
	}
}


__attribute__((constructor(101))) static void n3ds_early_init() {
	osSetSpeedupEnable(true);    // New 3DS: 804 MHz + L2 cache
	std::set_terminate(n3ds_terminate);
	std::set_new_handler(n3ds_out_of_memory);

	mkdir("sdmc:/3ds", 0777);
	mkdir(N3DS_HOME, 0777);
	mkdir(N3DS_HOME "/saves", 0777);    // same layout as the PC: saves/bg, saves/si
	mkdir(N3DS_HOME "/saves/bg", 0777);
	mkdir(N3DS_HOME "/saves/si", 0777);
	chdir(N3DS_HOME);
	setenv("HOME", N3DS_HOME, 1);

	if (!file_exists(N3DS_HOME "/exult.cfg")) {
		if (FILE* f = fopen(N3DS_HOME "/exult.cfg", "w")) {
			fputs(default_cfg, f);
			fclose(f);
		}
	} else if (!file_exists(N3DS_HOME "/.cfg-v2")) {
		migrate_cfg();
	}
	if (FILE* f = fopen(N3DS_HOME "/.cfg-v2", "w")) {    // one-time settings update done
		fclose(f);
	}

	if (__3dslink_host.s_addr != 0) {
		// Started over the network by 3dslink: send all output to the PC.
		soc_buffer = static_cast<u32*>(memalign(0x1000, SOC_BUFFER_SIZE));
		if (soc_buffer && R_SUCCEEDED(socInit(soc_buffer, SOC_BUFFER_SIZE))) {
			if (link3dsStdio() >= 0) {
				linked_to_pc = true;
				atexit([] { socExit(); });
				printf("[exult-3ds] connected to %s, output streaming here\n", inet_ntoa(__3dslink_host));
				log_memory();
				return;
			}
			socExit();
		}
	}
	// Route stdout/stderr (fds 1 and 2) to exult.log through our own device,
	// so the bottom-screen console can borrow them briefly for the legend.
	log_file = fopen(N3DS_HOME "/exult.log", "w");
	log_t0   = osGetTime();
	if (log_file) {
		setvbuf(log_file, nullptr, _IOFBF, 32 * 1024);
	}
	log_dotab.name       = "exultlog";
	log_dotab.structSize = 0;
	log_dotab.write_r    = log_write;
	install_log();
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	log_memory();
}

static PrintConsole bottom_console;


void n3ds_bottom_init();

extern "C" void n3ds_after_sdl_init() {
	n3ds_bottom_init();
	if (linked_to_pc) {
		// The bottom-screen console would take stdout back from the network
		// link, so print the legend to the PC terminal instead.
		puts("[exult-3ds] controls: touch=point+click, C-stick=cursor, CirclePad=walk,\n"
		     "  A=click/drag B=walk-to-cursor X=use Y=inventory, D-pad ^Map vStats <>targets,\n"
		     "  R=combat ZL=keys ZR=target START=menu SELECT=quicksave; hold L: ^Watch vPause <Lockpick >Food,\n"
		     "  L+SELECT=quick-restore L+Y=help L+X=next track");
		fflush(stdout);
		return;
	}
	consoleInit(GFX_BOTTOM, &bottom_console);
	consoleSelect(&bottom_console);
	printf("%s", "\x1b[2J\x1b[1;1H");
	printf("%s", "\x1b[33m  EXULT - Ultima VII for New 3DS\x1b[0m\n");
	printf("%s", "  ------------------------------------\n");
	printf("%s", "  Touch here   point + click (top scr)\n");
	printf("%s", "  C-Stick      move cursor\n");
	printf("%s", "  Circle Pad   walk\n");
	printf("%s", "  A  click/drag    B  walk to cursor\n");
	printf("%s", "  X  use/talk      Y  inventory\n");
	printf("%s", "  D-Pad  ^Map vStats  <> nearby targets\n");
	printf("%s", "  R  combat   ZL keys   ZR target\n");
	printf("%s", "  START menu  SELECT quicksave\n");
	printf("%s", "  ------------------------------------\n");
	printf("%s", "  Hold L for:\n");
	printf("%s", "  D-Pad  ^Watch vPause <Lockpick >Food\n");
	printf("%s", "  SELECT quick-restore   Y help\n");
	printf("%s", "  X next music track\n");
	printf("%s", "  ------------------------------------\n");
	if (!file_exists("sdmc:/3ds/dspfirm.cdc")) {
		printf("%s", "\x1b[31m  NO SOUND: /3ds/dspfirm.cdc missing\n  (run DSP1 once to create it)\x1b[0m\n");
	}
	printf("%s", "  Log: sdmc:/3ds/exult/exult.log\n");
	fflush(stdout);
	install_log();    // back to exult.log
	if (!file_exists("sdmc:/3ds/dspfirm.cdc")) {
		printf("[exult-3ds] sdmc:/3ds/dspfirm.cdc not found - audio will be silent\n");
	}
}

// --- glob() for newlib/3DS -------------------------------------------------
// Supports wildcards (* and ?) in the final path component, which is all
// Exult uses (savegames, mods, patches). Case-insensitive like FAT.

static bool wild_match(const char* pat, const char* str) {
	while (*pat) {
		if (*pat == '*') {
			while (*pat == '*') {
				pat++;
			}
			if (!*pat) {
				return true;
			}
			for (; *str; str++) {
				if (wild_match(pat, str)) {
					return true;
				}
			}
			return false;
		}
		if (!*str) {
			return false;
		}
		if (*pat != '?' && tolower(static_cast<unsigned char>(*pat)) != tolower(static_cast<unsigned char>(*str))) {
			return false;
		}
		pat++;
		str++;
	}
	return *str == '\0';
}

extern "C" int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* pglob) {
	(void)flags;
	(void)errfunc;
	std::string pat(pattern);
	std::string dir, mask;
	const auto  slash = pat.find_last_of('/');
	if (slash == std::string::npos) {
		dir  = ".";
		mask = pat;
	} else {
		dir  = pat.substr(0, slash);
		mask = pat.substr(slash + 1);
		if (dir.empty()) {
			dir = "/";
		}
	}

	std::vector<std::string> hits;
	if (DIR* d = opendir(dir.c_str())) {
		while (dirent* e = readdir(d)) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
				continue;
			}
			if (wild_match(mask.c_str(), e->d_name)) {
				hits.push_back(slash == std::string::npos ? std::string(e->d_name) : dir + "/" + e->d_name);
			}
		}
		closedir(d);
	}

	pglob->gl_pathc = hits.size();
	pglob->gl_pathv = static_cast<char**>(calloc(hits.size() + 1, sizeof(char*)));
	for (size_t i = 0; i < hits.size(); i++) {
		pglob->gl_pathv[i] = strdup(hits[i].c_str());
	}
	return hits.empty() ? 3 : 0;    // Exult expects glibc's value (3) for "no match"
}

extern "C" void globfree(glob_t* pglob) {
	if (!pglob->gl_pathv) {
		return;
	}
	for (size_t i = 0; i < pglob->gl_pathc; i++) {
		free(pglob->gl_pathv[i]);
	}
	free(pglob->gl_pathv);
	pglob->gl_pathv = nullptr;
	pglob->gl_pathc = 0;
}

// --- Touch UI hook ----------------------------------------------------------
// Exult only opens an on-screen keyboard for name fields when a TouchUI
// exists (Android/iOS). On 3DS the "controls" are the physical buttons, so the
// show/hide calls are no-ops; text entry goes to the 3DS system keyboard via
// SDL's swkbd support.
#include "touchui.h"

class TouchUI_N3DS : public TouchUI {
public:
	void showGameControls() override {}
	void hideGameControls() override {}
	void showButtonControls() override {}
	void hideButtonControls() override {}
	void showPauseControls() override {}
	void hidePauseControls() override {}
	void onDpadLocationChanged() override {}
};

TouchUI* n3ds_create_touchui() {
	return new TouchUI_N3DS();
}
