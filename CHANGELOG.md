# Changelog

## 2026-11-21

- Your settings (exult.cfg) are written safely: to a temporary file, then renamed, with the old one kept as exult.cfg.bak.
- Party alerts (badly hurt, poisoned, paralyzed) are tracked per person, are not shown for a game that is just loading, are not lost when two happen at once, and are not shown for the dead.
- Quieter touch click.
- The saved game is never kept in the RAM cache.
- The port builds with all compiler warnings on.

## 2026-11-20

- Exit save is safer: a backup copy of your game files (gamedat.bak) is made first, and EXIT GAME asks EXIT ANYWAY / NO instead of quitting when it cannot save (during a conversation, a fight or with a window open, or when the SD card is full).
- HOME > Close saves (when it can) and quits; it no longer hangs during a conversation.
- Write failures are noticed and logged.
- Detailed logging (launch timing, stall watch, Exult's chatter) only with an empty /3ds/exult/debug.txt.
- An original 3DS or 2DS is told that Exult needs a New 3DS.
- Install path: the program is /3ds/exult.3dsx. The START menu shows the build version.
- Fixed a crash when the game quit on a fatal error.

## 2026-11-19

- Party alerts appear in the game's own centred text.
- The dashboard minimap shows foes as a red X, not only a red dot.
- Manual: drag the stylus to scroll the text, with a little momentum.
