// selftest_port.h - Exult's additions to the shared selftest.c (3ds-test-suite hwtest/selftest).
// selftest.c includes this file twice: ST_PORT_CONFIG (settings) and ST_PORT_CODE (extra code).
#ifdef ST_PORT_CONFIG
// Exult: ask the game to quit the normal way when the script is done (Nov 18 2026), as EXIT GAME ->
// YES does, from the watchdog and from aptMainLoop (the frontend loop), before SDL turns it into a
// close request (n3ds_bottom.cc's HOME -> Close handling would quicksave the game: EX-04). Ended
// from the watchdog with svcExitProcess, its memory was not given back and the next program got
// ~1 MB less heap; so the watchdog ends it only after 20 s.
#ifdef __cplusplus
extern "C"
#endif
void n3ds_selftest_quit(void) __attribute__((weak));   // n3ds_bottom.cc
#define ST_PORT_QUIT() do { if (n3ds_selftest_quit) n3ds_selftest_quit(); } while (0)
#define ST_APT_QUIT 1
#define ST_DONE_GRACE_MS 20000
#endif
#ifdef ST_PORT_CODE
// Always-on crash restart (3ds-test-suite hwtest/restartguard): compiled into the selftest object
// so it is always linked.
#include "restartguard.c"
#endif
