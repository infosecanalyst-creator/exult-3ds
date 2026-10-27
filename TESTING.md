# Testing and change history

Notes from development and testing. The build is tested in the Azahar emulator (New 3DS mode), with real-hardware results added as they come in.

## Touch-first layout (Oct 26-27)
- **Why:** on hardware, copying the top screen into a live view on the bottom lagged badly. The game now runs on the touchscreen at 320x240, pixel for pixel, and the top screen is a dashboard. "Runs a lot faster" on hardware.
- **Display:** SDL copies the middle 320 columns of the 400x240 window to the bottom screen, or a zoomed rectangle (`N3DS_BottomView`). Overlays (menu button, menu, answer buttons) are drawn after the copy (`N3DS_BottomOverlay`). Taps map 1:1, including with zoom.
- **Swap (L+START):** game back on top (32-bit, double-buffered, 3D), dashboard on the bottom (RGB565).
- **Exult's menus and title scenes:** laid out in the 320-pixel middle (single-column game picker; scenes centred, not stretched).
- **Top-screen views:** conversations (portrait at 2x and words; answers as buttons below), books, scrolls, signs (runes plus plain letters), world map (BG), party stats.
- **Flicker fix:** the overlay is drawn once per game frame. Pending changes are presented directly only when the game is paused.
- **In-game manual:** replaces the 6-page guide.
- **Tested in the emulator:** picker, title, dashboard fonts, zoom with taps, menu, portrait to inventory, swap both ways, map, stats, a conversation with Iolo (answers tapped), manual sections. Books and signs aren't tested yet.

## Slow starts on hardware (open)
- Some launches take minutes and others seconds, with the same build.
- **Ruled out by measurement:** SD read speed (9 MB/s), SD opens and stats (none slow when it happened), memory and CPU speed at launch (all normal).
- In slow runs everything after the audio and SDL setup is ~100x slower, including pure computation. Suspect: SDL's audio thread falls back to the game's core at a higher priority when the system core is refused, so FM synthesis starves the game. It now runs below the game in that case, and the log records each thread's core and priority.
- The log has a heartbeat when the game goes quiet (last SD call, calls since).

## Readability: pixel font and text background (Oct 25)
- Three options were compared on the same conversation:
  - A: DejaVu Sans Bold, the previous version.
  - B: U7 Pixel Sans, hand-drawn with 9 px capitals and a 6 px x-height in a 14 px line, so every stroke sits on the pixel grid.
  - C: U7 Pixel Sans plus Exult's dark text background.
- C was chosen and is the default.
- Existing configs switch to the dark text background once (marker file `.cfg-v4`, `textbackground` set to 15). Checked in the emulator, starting from `-1`.

## Modern fonts (Oct 25)
- The yellow blackletter text fonts (Exult fonts 0 and 7) are replaced by DejaVu Sans Bold 10 px, in palette colour 118 (236,236,236) with a black outline. The 14 px line height and the baseline are unchanged, so Exult's text layouts still fit. Bullets and other special characters were recoloured as well.
- Checked in the emulator: Iolo's conversation, the topic keywords, the answer buttons on the bottom screen, and item names.
- Existing configs switch to `fonts=modern` once (marker file `.cfg-v3`). "Modern" was added to Exult's Display options, so pressing OK there doesn't silently switch back.

## In-game guide and tips (Oct 25)
- The **?** guide has 8 pages: Welcome, The live picture, The party strip, The info column, Talking to people, The action bar, Buttons, and 3D/saving/tips. All 8 checked in the emulator, with Back, Next, Close and Show tips again working.
- **First-time tips:** the welcome tip appeared about 4 s into the game; after it was closed, the portrait tip followed about 8 s later. Seen tips are stored in `sdmc:/3ds/exult/tips.dat`.
- The pre-game screen is updated for the touch panel.

## Startup speed (Oct 25)
- Exult re-opens its data files many times: `exult.flx` about 20 times during startup, and the multi-MB shape files several times. The earlier loader read the whole file on every open, which came to more than 100 MB of SD reads before the game started.
- Files up to 9 MB are now read once and shared from a 24 MB RAM cache. Larger files are read in 32 KB pieces, so only the parts Exult asks for are read.
- Emulator, Black Gate:

  | | Before | After |
  |---|---|---|
  | Startup to the menu | 4.6 s | 2.8 s |
  | Journey Onward load | about 38 s | about 29 s |
  | Data read whole | more than 100 MB | 25.8 MB |
  | Heap in use (of 96 MB) | | 32 MB |
- `exult.log` now records, once per session, how much was read and cached when the game first appears.

## Tested in the Azahar emulator (New 3DS mode) with Black Gate and Serpent Isle (GOG data)
- **Serpent Isle:**
  - The menu, character creation and the opening scene on the ship work.
  - The party strip shows the SI portraits (Iolo, Shamino, Dupre).
  - Saving and Journey Onward work.
- **3D:** checked in the emulator's side-by-side mode in Trinsic and on SI's ship.
  - Roofs, tree tops and walls come forward, and the ground sits behind.
  - Windows and text stay flat.
- **Minimap:** tapping across Trinsic walked the Avatar there, and the party followed.
- **Fixed a file-writing bug that could corrupt data.** Files that weren't closed explicitly lost their first 8 bytes. This broke the start of every new Serpent Isle game, and could affect other files.
  - If Serpent Isle closed itself when you started a new game with the earlier build, just start a new game again.
  - Black Gate saves were checked and are fine.
- **Startup and loading:**
  - Startup to the menu takes about 5 seconds.
  - A new game takes about 30 seconds, and Journey Onward about 25.
  - Memory: 96 MB for the game and 22 MB for graphics and sound.
- **Bottom screen:**
  - Portraits update live, and tapping one opens that member's inventory.
  - Dialogue answer buttons work: job and bye with Iolo.
  - The action bar's Map and Save buttons work.
  - The under-cursor name shows ("Iolo", "slate roof"), and the help overlay opens.
- **Controls:** D-pad target cycling goes Iolo first, then signs, then the lamp post.
- **Your PC save works on the 3DS:**
  - With the Keyring mod chosen from Show Mods, your PC save `exult00bg.sav` appeared in Load/Save and loaded correctly.
  - The 3DS keeps mod saves in `saves/bg/mods/Keyring/saves`, the same as the PC.
- **Save sync:**
  - Tested against a stand-in for the 3DS's FTP server: first sync with a clash, nothing changed, played on the 3DS, played on the PC, and the copy of the game in progress.
  - No save was lost or overwritten in any case.
- **Autosave:** it fires only when you're idle and writes the game in progress.

Not yet tested:
- Running on a real 3DS, including speed and the real cost of 3D.
- Sound on hardware.

## Interface review (Oct 25)
I compared the interface with stylus-driven DS/3DS games, mouse-driven CRPGs, and touch versions of PC RPGs (including Exult's own iOS/Android settings). Two things changed:
- **What you see is what you tap.** The middle of the bottom screen used to be a blank touchpad, so you had to guess where on the top screen a touch would land. It now shows a live copy of the top screen at the correct proportions. A tap lands exactly where you see it.
- **Fewer steps:**
  - Walking anywhere is now one gesture: hold the stylus still. It uses Exult's touch path-finding, the same as the phone versions.
  - Identifying something no longer needs a separate window: the info column shows names, health and item counts as soon as the cursor touches them.
- Older configs are updated once, at the first launch of this build, to turn hold-to-walk on.
- **Round two:**
  - Drag items onto party portraits.
  - Hold L to zoom.
  - Status tags and plain-English alerts in the party strip.
  - The bottom screen now also updates while the game is paused, for example with an inventory open. Before this, it froze until something moved on the top screen.

## Fixes from the first hardware run (Oct 25)
- **The Avatar couldn't move.** My default config set Exult's `fps` to 30. In Exult that setting is the game speed, not the display frame rate: 5 is normal, so 30 ran the game six times too fast. Opening Exult's Game Engine options then saved a garbage value, which froze the game timer.
  - The default is now 5.
  - Exult now ignores out-of-range values, and the options screen no longer writes garbage.
  - Your 3DS's `exult.cfg` was corrected over FTP.
- **The log is lighter and timestamped.** Each line of `exult.log` starts with seconds since launch, so slow startup steps show up. The log is now written to the SD card once a second instead of on every line.

