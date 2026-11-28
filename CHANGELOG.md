# Changelog

## 2026-11-27

- START menu: GAME OPTIONS is now called OPTIONS, the name the other 3DS ports use (cross-port pass). It opens the same Exult game menu. SAVE / LOAD stays one row: Exult's own save screen does both.

## 2026-11-22

- New Game asks before it replaces a game in progress (starts on NO), and the old game is kept as a save called "Before new game" (Load Game brings it back).
- During a conversation or while reading, SAVE / LOAD and GAME OPTIONS in the START menu are greyed with the reason; they used to skip a line of the talk. SELECT is ignored then too.
- EXIT GAME > YES during a talk, a fight or with a window open says "Not saved!" at once (it used to close the menu and quit later by itself).
- L+SELECT (quick-restore) asks first.
- The autosave waits for a quiet moment (a line of talk on screen counts as busy) and tries again after 30 seconds.
- While the START menu or the manual is open, no button or stick reaches the game.
- Words over people's heads stay inside the screen (no more "peak to thee").
- The dashboard always uses the daylight colours: readable in the red opening scene and at night.
- Dashboard: a button-hint line under the messages.
- START menu footer follows left-hand mode; so does the start screen.
- Start screen: "Exit to the Homebrew Launcher" button; "B: back" only where it works; it explains Continue and names START.
- Loading: a styled page with a bar that moves while the SD card is read, instead of the raw text screen.
- Exit question says where the game is saved ("the game in progress; Continue picks it up").
- Manual: the section being read has a gold ring instead of a gold face. Text updated to match all of the above.

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
