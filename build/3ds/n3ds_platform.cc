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
#include <fcntl.h>
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
		"\t\t<game>\n\t\t\t<width>\n\t\t\t320\n\t\t\t</width>\n"
		"\t\t\t<height>\n\t\t\t240\n\t\t\t</height>\n\t\t</game>\n"
		"\t\t<width>\n\t\t400\n\t\t</width>\n"
		"\t\t<height>\n\t\t240\n\t\t</height>\n"
		"\t\t<scale>\n\t\t1\n\t\t</scale>\n"
		"\t\t<scale_method>\n\t\tpoint\n\t\t</scale_method>\n"
		"\t\t<fill_mode>\n\t\tCentre\n\t\t</fill_mode>\n"
		"\t\t<fill_scaler>\n\t\tpoint\n\t\t</fill_scaler>\n"
		"\t\t<fullscreen>\n\t\tyes\n\t\t</fullscreen>\n"
		"\t\t<share_video_settings>\n\t\tyes\n\t\t</share_video_settings>\n"
		"\t\t<fps>\n\t\t5\n\t\t</fps>\n"
		"\t\t<ui>\n\t\t\t<universal>\n\t\t\tyes\n\t\t\t</universal>\n"
		"\t\t\t<width>\n\t\t\t320\n\t\t\t</width>\n"
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
		"\t<gameplay>\n\t\t<skip_splash>\n\t\tyes\n\t\t</skip_splash>\n\t\t<fonts>\n\t\tmodern\n\t\t</fonts>\n\t\t<textbackground>\n\t\t15\n\t\t</textbackground>\n\t</gameplay>\n"
		"</config>\n";

static FILE*      log_file = nullptr;
static devoptab_t log_dotab {};

// Each line gets a "[seconds]" stamp so slow spots show up in the log. The
// file is buffered and flushed at most once a second (or on a FATAL line):
// flushing every line costs an SD round trip each time, and Exult logs a
// lot during play (NPC path-finding).
extern "C" unsigned long long n3ds_ticks() {
	return svcGetSystemTick();
}
extern "C" double n3ds_io_ms();    // files/utils.cc: time spent waiting on the SD card

static u64  log_t0         = 0;
static u64  log_last_flush = 0;
static bool log_at_bol     = true;

static LightLock log_lock;
static bool      log_lock_ready = false;
static volatile u64 log_last_line = 0;    // when the game last logged anything

static ssize_t log_write(struct _reent*, void*, const char* ptr, size_t len) {
	if (log_lock_ready) {
		LightLock_Lock(&log_lock);
	}
	log_last_line = osGetTime();
	if (log_file) {
		const u64 now = osGetTime();
		for (size_t i = 0; i < len;) {
			if (log_at_bol) {
				fprintf(log_file, "[%6.1f io %5.1f] ", (now - log_t0) / 1000.0, n3ds_io_ms() / 1000.0);
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
	if (log_lock_ready) {
		LightLock_Unlock(&log_lock);
	}
	return static_cast<ssize_t>(len);
}

// Push buffered log lines to the SD card (called every few seconds from
// the bottom-screen tick, so a quiet log still lands on the card).
extern "C" void n3ds_log_flush() {
	if (log_file) {
		fflush(log_file);
		log_last_flush = osGetTime();
	}
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
	const bool v2 = !file_exists(N3DS_HOME "/.cfg-v2");    // hold-to-walk, game speed
	const bool v3 = !file_exists(N3DS_HOME "/.cfg-v3");    // modern fonts
	const bool v4 = !file_exists(N3DS_HOME "/.cfg-v4");    // dark box behind conversation text
	const bool v5 = !file_exists(N3DS_HOME "/.cfg-v5");    // game on the touchscreen: 320x240
	if (!v2 && !v3 && !v4 && !v5) {
		return;
	}
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
	if (v2) {
		replace("<touch_pathfind>no</touch_pathfind>", "<touch_pathfind>yes</touch_pathfind>");
		const size_t a = cfg.find("<fps>"), b = cfg.find("</fps>");
		if (a != std::string::npos && b != std::string::npos && b > a) {
			const int fps = atoi(cfg.c_str() + a + 5);
			if (fps <= 0 || fps > 20) {
				cfg.replace(a + 5, b - a - 5, "5");
				changed = true;
			}
		}
	}
	if (v3) {    // switch to the readable fonts once (the options screen can switch back)
		const size_t a = cfg.find("<fonts>"), b = cfg.find("</fonts>");
		if (a != std::string::npos && b != std::string::npos && b > a) {
			cfg.replace(a + 7, b - a - 7, "modern");
			changed = true;
		} else {
			const size_t g = cfg.find("</gameplay>");
			if (g != std::string::npos) {
				cfg.insert(g, "<fonts>modern</fonts>\n  ");
				changed = true;
			}
		}
	}
	if (v4) {    // Exult's "text background": 15 = dark grey, translucent
		const size_t a = cfg.find("<textbackground>"), b = cfg.find("</textbackground>");
		if (a != std::string::npos && b != std::string::npos && b > a) {
			if (atoi(cfg.c_str() + a + 16) < 0) {
				cfg.replace(a + 16, b - a - 16, "15");
				changed = true;
			}
		} else {
			const size_t g = cfg.find("</gameplay>");
			if (g != std::string::npos) {
				cfg.insert(g, "<textbackground>15</textbackground>\n  ");
				changed = true;
			}
		}
	}
	if (v5) {
		// The game is played on the 320x240 touchscreen: game and UI areas
		// 320x240, centred (unscaled) in the 400x240 window.
		auto set_in = [&](const char* section, const char* tag, const char* value) {
			const size_t sec = cfg.find(section);
			if (sec == std::string::npos) {
				return;
			}
			const size_t end = cfg.find(std::string("</") + (section + 1), sec);
			const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
			const size_t a = cfg.find(open, sec), b = cfg.find(close, sec);
			if (a == std::string::npos || b == std::string::npos || b < a || (end != std::string::npos && b > end)) {
				return;
			}
			cfg.replace(a + open.size(), b - a - open.size(), std::string("\n") + value + "\n");
			changed = true;
		};
		const size_t video = cfg.find("<video>");
		if (video != std::string::npos) {
			std::string rest = cfg.substr(video);
			std::string head = cfg.substr(0, video);
			cfg              = rest;
			set_in("<game>", "width", "320");
			set_in("<ui>", "width", "320");
			cfg = head + cfg;
			const size_t a = cfg.find("<fill_mode>", video), b = cfg.find("</fill_mode>", video);
			if (a != std::string::npos && b != std::string::npos && b > a) {
				cfg.replace(a + 11, b - a - 11, "\nCentre\n");
				changed = true;
			}
		}
	}
	if (changed) {
		if (FILE* o = fopen(N3DS_HOME "/exult.cfg", "wb")) {
			fwrite(cfg.data(), 1, cfg.size(), o);
			fclose(o);
		}
	}
	for (const char* marker : {N3DS_HOME "/.cfg-v2", N3DS_HOME "/.cfg-v3", N3DS_HOME "/.cfg-v4", N3DS_HOME "/.cfg-v5"}) {
		if (FILE* m = fopen(marker, "w")) {
			fclose(m);
		}
	}
}


static void n3ds_bench();

// --- SD access monitor ---------------------------------------------------------
// Wraps the SD card's device driver to time every open, stat and directory
// scan, and logs the slow ones (with the file name) and totals. Some launches
// stall for minutes on file opens while reads run at full speed.
static devoptab_t sd_orig, sd_wrap;
static u64        sdm_open_t = 0, sdm_stat_t = 0, sdm_dir_t = 0, sdm_close_t = 0;
static unsigned   sdm_opens = 0, sdm_stats = 0, sdm_dirs = 0, sdm_slow = 0;
static bool       sdm_in = false;

static char         sdm_last[160];
static volatile u64 sdm_ops = 0;

static void sdm_mark(const char* what, const char* path) {
	snprintf(sdm_last, sizeof sdm_last, "%s %s", what, path ? path : "?");
	sdm_ops++;
}

static void sdm_note(const char* what, const char* path, u64 dt) {
	const double ms = dt / 268111.856;
	if (ms >= 150 && !sdm_in && sdm_slow < 200) {
		sdm_in = true;
		sdm_slow++;
		printf("[exult-3ds] slow SD %s %.0f ms: %s\n", what, ms, path ? path : "?");
		sdm_in = false;
	}
}

static int sdm_open(struct _reent* r, void* fs, const char* path, int flags, int mode) {
	sdm_mark((flags & (O_WRONLY | O_RDWR)) ? "open-write" : "open", path);
	const u64 t0 = svcGetSystemTick();
	const int rv = sd_orig.open_r(r, fs, path, flags, mode);
	const u64 dt = svcGetSystemTick() - t0;
	sdm_open_t += dt;
	sdm_opens++;
	sdm_note((flags & (O_WRONLY | O_RDWR)) ? "open-write" : "open", path, dt);
	return rv;
}

static int sdm_close(struct _reent* r, void* fd) {
	const u64 t0 = svcGetSystemTick();
	const int rv = sd_orig.close_r(r, fd);
	const u64 dt = svcGetSystemTick() - t0;
	sdm_close_t += dt;
	sdm_note("close", "(file)", dt);
	return rv;
}

static int sdm_stat(struct _reent* r, const char* path, struct stat* st) {
	sdm_mark("stat", path);
	const u64 t0 = svcGetSystemTick();
	const int rv = sd_orig.stat_r(r, path, st);
	const u64 dt = svcGetSystemTick() - t0;
	sdm_stat_t += dt;
	sdm_stats++;
	sdm_note("stat", path, dt);
	return rv;
}

static DIR_ITER* sdm_diropen(struct _reent* r, DIR_ITER* d, const char* path) {
	sdm_mark("diropen", path);
	const u64 t0  = svcGetSystemTick();
	DIR_ITER* rv  = sd_orig.diropen_r(r, d, path);
	const u64 dt  = svcGetSystemTick() - t0;
	sdm_dir_t += dt;
	sdm_dirs++;
	sdm_note("diropen", path, dt);
	return rv;
}

static void sdm_install() {
	const int idx = FindDevice("sdmc:");
	if (idx < 0 || !devoptab_list[idx]) {
		return;
	}
	sd_orig = *devoptab_list[idx];
	sd_wrap = sd_orig;
	if (sd_orig.open_r) sd_wrap.open_r = sdm_open;
	if (sd_orig.close_r) sd_wrap.close_r = sdm_close;
	if (sd_orig.stat_r) sd_wrap.stat_r = sdm_stat;
	if (sd_orig.diropen_r) sd_wrap.diropen_r = sdm_diropen;
	devoptab_list[idx] = &sd_wrap;
}

// Heartbeat: when the game has logged nothing for 3 seconds, note what it was
// last doing on the SD card and how many SD calls it has made since, so a
// stall (or a crawl) shows up in the log with its context.
static void heartbeat(void*) {
	u64      last_ops = 0;
	unsigned beats    = 0;
	for (;;) {
		svcSleepThread(2000000000LL);
		if (!log_file) {
			continue;
		}
		const u64 now   = osGetTime();
		const u64 quiet = now - log_last_line;
		if (quiet < 3000) {
			last_ops = sdm_ops;
			beats    = 0;
			continue;
		}
		// Note a quiet spell at about 3, 10 and 30 seconds, then once a minute.
		static const u64 marks[] = {3000, 10000, 30000};
		const u64        due     = beats < 3 ? marks[beats] : 30000 + (beats - 2) * 60000ull;
		if (quiet < due) {
			continue;
		}
		beats++;
		LightLock_Lock(&log_lock);
		fprintf(log_file, "[%6.1f hb] quiet %.0f s; SD calls since last beat %llu; last: %s\n", (now - log_t0) / 1000.0,
				(now - log_last_line) / 1000.0, static_cast<unsigned long long>(sdm_ops - last_ops), sdm_last);
		fflush(log_file);
		LightLock_Unlock(&log_lock);
		last_ops = sdm_ops;
	}
}

static void heartbeat_start() {
	LightLock_Init(&log_lock);
	log_lock_ready = true;
	s32 prio       = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	// Above the game (it sleeps almost all the time), on the game's core.
	threadCreate(heartbeat, nullptr, 16 * 1024, prio > 0x20 ? prio - 8 : 0x19, -2, true);
}

extern "C" void n3ds_sd_report(char* out, unsigned n) {
	snprintf(out, n, "SD: %u opens %.1f s, %u stats %.1f s, %u dir scans %.1f s, closes %.1f s, %u slow", sdm_opens,
			 sdm_open_t / 268111856.0, sdm_stats, sdm_stat_t / 268111856.0, sdm_dirs, sdm_dir_t / 268111856.0,
			 sdm_close_t / 268111856.0, sdm_slow);
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
	} else {
		migrate_cfg();    // one-time updates, each guarded by its own marker file
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
	sdm_install();
	heartbeat_start();
	n3ds_bench();
}

// Quick hardware check written to the log at every launch: CPU clock (is the
// New 3DS 804 MHz mode really on?), memory speed and SD card speed.
static unsigned char bench_bss[1 << 20];

static double ms_since(u64 t0) {
	return (svcGetSystemTick() - t0) / 268111.856;
}

static void n3ds_bench() {
	u64 t0 = svcGetSystemTick();
	bool n3ds = false;
	APT_CheckNew3DS(&n3ds);
	const double apt_ms = ms_since(t0);
	t0                  = svcGetSystemTick();
	const Result ptm    = ptmSysmInit();
	if (R_SUCCEEDED(ptm)) {
		ptmSysmExit();
	}
	const double ptm_ms = ms_since(t0);
	// 100M dependent adds: ~1 cycle each -> ~124 ms at 804 MHz, ~373 ms at 268 MHz.
	t0       = svcGetSystemTick();
	u32 acc  = 0;
	for (u32 i = 0; i < 100000000u; i++) {
		__asm__ volatile("add %0, %0, #1" : "+r"(acc));
	}
	const double add_ms = ms_since(t0);
	auto mbps = [](double bytes, double ms) { return ms > 0 ? bytes / 1048576.0 / (ms / 1000.0) : 0.0; };
	// Normal heap
	t0 = svcGetSystemTick();
	auto* h = static_cast<unsigned char*>(malloc(4 << 20));
	const double malloc_ms = ms_since(t0);
	t0 = svcGetSystemTick();
	for (int k = 0; k < 4; k++) {
		memset(h, k, 4 << 20);
	}
	const double heap_set = mbps(16 << 20, ms_since(t0));
	auto* h2 = static_cast<unsigned char*>(malloc(4 << 20));
	t0 = svcGetSystemTick();
	for (int k = 0; k < 4; k++) {
		memcpy(h2, h, 4 << 20);
		h[k * 4096] ^= h2[k * 1000 + 7];
	}
	const double heap_cpy = mbps(16 << 20, ms_since(t0));
	// Linear heap
	auto* l = static_cast<unsigned char*>(linearAlloc(4 << 20));
	double lin_set = 0;
	if (l) {
		t0 = svcGetSystemTick();
		for (int k = 0; k < 4; k++) {
			memset(l, k, 4 << 20);
		}
		lin_set = mbps(16 << 20, ms_since(t0));
		linearFree(l);
	}
	// Program's own data (.bss)
	t0 = svcGetSystemTick();
	for (int k = 0; k < 16; k++) {
		memset(bench_bss, k, sizeof bench_bss);
	}
	const double bss_set = mbps(16 << 20, ms_since(t0));
	// Small allocations, like Exult makes by the thousand
	t0 = svcGetSystemTick();
	std::vector<void*> small;
	for (int k = 0; k < 20000; k++) {
		small.push_back(malloc(16 + (k % 200)));
	}
	for (void* q : small) {
		free(q);
	}
	const double small_ms = ms_since(t0);
	const unsigned sink   = h[123] + h2[456] + acc;
	free(h);
	free(h2);
	double sd_ms = 0, sd_mb = 0;
	std::vector<char> rd(1 << 20);
	if (FILE* f = fopen(N3DS_HOME "/data/exult.flx", "rb")) {
		setvbuf(f, nullptr, _IONBF, 0);
		t0         = svcGetSystemTick();
		size_t got = 0, r;
		while (got < rd.size() && (r = fread(rd.data() + got, 1, 256 * 1024, f)) > 0) {
			got += r;
		}
		sd_ms = ms_since(t0);
		sd_mb = got / 1048576.0;
		fclose(f);
	}
	printf("[exult-3ds] check: New3DS %s (%.0f ms), ptm:sysm %s (%.0f ms), 100M adds %.0f ms (~%.0f MHz)\n",
		   n3ds ? "yes" : "no", apt_ms, R_SUCCEEDED(ptm) ? "ok" : "DENIED", ptm_ms, add_ms, 100000.0 / add_ms);
	printf("[exult-3ds] check: malloc 4MB %.1f ms, memset heap %.0f MB/s, memcpy heap %.0f MB/s, memset linear %.0f MB/s, "
		   "memset bss %.0f MB/s, 20k small mallocs %.0f ms, SD %.1f MB/s (%u)\n",
		   malloc_ms, heap_set, heap_cpy, lin_set, bss_set, small_ms, sd_ms > 0 ? sd_mb / (sd_ms / 1000.0) : 0.0, sink & 1);
	fflush(stdout);
}

static PrintConsole top_console;
static bool         console_ready = false;

void n3ds_bottom_init();

// The top screen while Exult's own menus are up (they're on the touchscreen).
extern "C" void n3ds_show_legend() {
	if (linked_to_pc) {
		return;
	}
	if (!console_ready) {
		consoleInit(GFX_TOP, &top_console);
		console_ready = true;
	} else {
		consoleInit(GFX_TOP, &top_console);    // screen format was reset: set it up again
	}
	consoleSelect(&top_console);
	printf("%s", "\x1b[2J\x1b[1;1H");
	printf("%s", "\n\x1b[33m     EXULT - Ultima VII for New 3DS\x1b[0m\n");
	printf("%s", "     -----------------------------------------\n");
	printf("%s", "     The game is on the touchscreen below:\n");
	printf("%s", "     tap a menu item to choose it.\n\n");
	printf("%s", "     In the game:\n");
	printf("%s", "       tap = click     double-tap = use / talk\n");
	printf("%s", "       hold the stylus still = walk there\n");
	printf("%s", "       drag = move items\n");
	printf("%s", "       menu button (top right) = everything else\n\n");
	printf("%s", "     This screen then shows your party, a\n");
	printf("%s", "     minimap, the time and recent messages.\n\n");
	printf("%s", "     L+START  swap screens (3D on top)\n");
	printf("%s", "     L+Y      manual     START  game menu\n");
	printf("%s", "     ZL       zoom the touchscreen in / out\n");
	printf("%s", "     -----------------------------------------\n");
	if (!file_exists("sdmc:/3ds/dspfirm.cdc")) {
		printf("%s", "\x1b[31m     NO SOUND: /3ds/dspfirm.cdc missing\n     (run DSP1 once to create it)\x1b[0m\n");
	}
	printf("%s", "     Log: sdmc:/3ds/exult/exult.log\n");
	fflush(stdout);
	install_log();    // back to exult.log
}

extern "C" void n3ds_after_sdl_init() {
	n3ds_bottom_init();
	if (linked_to_pc) {
		// The console would take stdout back from the network link, so print
		// the legend to the PC terminal instead.
		puts("[exult-3ds] the game is on the touchscreen: tap=click, double-tap=use/talk, hold=walk,\n"
		     "  drag=move; menu button (top right) for the rest. Top screen = party, minimap, messages.\n"
		     "  L+START swap screens (3D), L+Y guide, START game menu, SELECT quicksave.");
		fflush(stdout);
		return;
	}
	n3ds_show_legend();
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
