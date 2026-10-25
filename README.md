# Exult (Ultima VII) for New 3DS XL

A port of the current Exult engine (1.13.1git, built against SDL3's 3DS backend) to the New 3DS. It plays **The Black Gate** (with Forge of Virtue) and **Serpent Isle** (with Silver Seed), using your own GOG game data.

## Install and play (Wi-Fi, no SD card swapping)

**Double-click `Exult-3DS.cmd`** and choose from the menu:

| # | What to do on the 3DS first | What it does |
|---|---|---|
| **1 Install / update** | Open **ftpd** | Finds the 3DS on your network by itself, then sends Exult and your Black Gate / Serpent Isle data, plus any mods (Keyring, SI Fixes) from `C:\Ultima` or `Desktop\Ultima Sources`. After the first time it only sends files that changed. If the transfer is interrupted, run it again and it picks up where it left off. |
| **2 Play with live log** | Homebrew Launcher, press **Y** | Sends Exult and launches it. Exult's output scrolls in the window on your PC. |
| 3 Get log + config | Open **ftpd** | Pulls `exult.log` and `exult.cfg` into `from-3ds\` and opens the log |
| 4 Send edited config | Open **ftpd** | Sends your edited `from-3ds\exult.cfg` back to the 3DS |
| 5 Full re-send | Open **ftpd** | Sends everything again, for example after reformatting the SD card |
| **6 Sync saves with PC** | Open **ftpd** | Shares saves both ways with your PC Exult (`C:\Ultima\Saves\Exult-BG` / `Exult-SI`, including Keyring and SI Fixes saves). See [Playing on both PC and 3DS](#playing-on-both-pc-and-3ds). |

**Day to day, you don't need the PC at all.** After step 1, Exult appears in the Homebrew Launcher, so start it from there like any other app. Option 2 is only for when you want to watch the log, for example while testing a new build.

**Where things live**
- The 3DS's address is remembered in `settings.json`, along with which files have already been sent.
- File transfers use `curl.exe`, which is built into Windows.
- Option 2 uses `3dslink.exe` from devkitPro, the same one used for SimGolf. It finds the 3DS automatically.

<details><summary>Offline alternative: SD card</summary>

`Copy-U7-To-3DS.ps1` copies the same files to the card while it's in the PC. The layout on the card:
```
sdmc:/3ds/exult/exult.3dsx
sdmc:/3ds/exult/data/        Exult's own data files
sdmc:/3ds/exult/blackgate/   STATIC/ (+ mods/)
sdmc:/3ds/exult/serpentisle/ STATIC/ (+ mods/)
```
</details>

## The bottom screen

When you're playing, the bottom screen becomes part of the game:

- **Party strip (top):**
  - Shows a portrait of each party member with health (red) and mana (blue) bars.
  - A portrait flashes red when that member is hit and green when poisoned.
  - **Tap a portrait** to open that member's inventory.
- **Middle area:**
  - Works as a **touchpad** for the top-screen cursor. Touching is a click, and you can drag items.
  - Shows the time of day, **the name of whatever the cursor is over**, and COMBAT when you're fighting.
- **map** (right-hand column) turns the middle area into a **live minimap** of the area around the party:
  - It shows terrain and roads, the party (green), other people (yellow, or red if hostile), and a box for what the top screen shows.
  - **Tap a spot on the minimap and the Avatar walks there**, finding the way around buildings and water. The party follows.
  - Tap **map** again to go back to the touchpad.
- **Dialogue:** the middle area turns into **large answer buttons**. Tap one to answer. If there are more than 10 answers, "More…" shows the rest.
- **Help:** **?** shows the controls.
- **Action bar (bottom):** Map · Spells · Notes · Keys · Pack / Combat · Target · Food · Save · Menu, using Exult's own icons. The Combat button lights up while you're fighting.

Before a game is loaded (in Exult's menus), the bottom screen shows the controls list, and the whole screen is a touchpad.

## Stereoscopic 3D

Push up the **3D slider**. The world gets real depth:
- The ground sits behind the screen.
- Walls, trees, people, upper floors and roofs rise towards you.
- Windows, dialogue and text stay sharp on the screen plane.

How it works: Exult tracks each pixel's height (an object's lift, rising towards the top of tall shapes), then builds the right-eye image from that. Moving the slider changes the depth.

With the slider all the way down, it's switched off and costs nothing. When it's up, it adds about 5 ms per frame in the emulator. Your log (`exult.log`, menu option 3) records the real cost on your 3DS as `3D right eye: … ms/frame`.

## Controls

| Input | Action |
|---|---|
| Touch, middle area | Point and click on the top screen (drag to move items) |
| **map** button, then tap | Minimap: walk to the tapped spot |
| 3D slider | Stereoscopic 3D depth (off at the bottom) |
| C-Stick | Move the cursor |
| Circle Pad | Walk |
| **D-Pad ← / →** | **Jump the cursor to the next nearby person or object** (people first, then containers, then other items). Press **X** to talk to it or use it. |
| D-Pad ↑ / ↓ | Map / Stats |
| A | Left click: select, or hold to drag |
| B | Right click: hold to walk toward the cursor |
| X | Double click: use or talk |
| Y | Inventory |
| R | Toggle combat |
| ZL / ZR | Try keys (Keyring) / Target mode |
| START | Game menu (save, load, options), or close open windows |
| SELECT | Quicksave |
| **Hold L** + D-Pad | ↑ Watch · ↓ Pause combat · ← Lockpick · → Food |
| L + SELECT / L + Y / L + X | Quick-restore / Help / Next music track |

Typing (the Avatar's name, save names) uses the 3DS system keyboard. To rename a save slot, tap it again.

## Playing on both PC and 3DS

Exult saves are the same on every platform, so you can play on the PC, sync, and keep going on the 3DS (or the reverse).

1. Open **ftpd** on the 3DS and choose **6 Sync saves with PC**.
2. On either machine, open START → Load/Save Game. Saves from the other machine are in the list.

How the sync behaves:
- It reads your PC save folders from `C:\Ultima\Config\exult.cfg`.
- It covers the base games and every mod (Keyring, SI Fixes).
- It **never deletes a save and never silently overwrites one:**
  - A slot changed on only one side is copied to the other.
  - A slot changed on both sides is kept twice: the 3DS version goes into a new slot on both machines.
- If the 3DS has no game in progress yet for a game or mod, the sync also copies the PC's current game, so **Journey Onward** works straight away.
- Mod saves only appear when you play that mod. Choose **Show Mods → Black Gate Keyring** in the Exult menu, as on your PC.

## Default settings (in `exult.cfg`)
- **Game area is 400×240 with no scaling.** That fills the top screen and shows more of the map than the original 320×200. You chose the wide view on the PC too, and the same caveat applies: the Skara Brae sacrifice scene can break in a wide view. If it does, change `video/game` to 320×200.
- **Music uses OPL synth (FMOpl), mono, 22 kHz.** This keeps the CPU free. The digital OGG soundtrack is turned off, because decoding it on the 3DS's ARM11 costs a lot of CPU.
- **804 MHz mode and the L2 cache are turned on at launch.**
- **The splash intro is skipped when you pick a game.** "View Introduction" is still in the game's menu.
- **Autosave:** every 15 minutes, while you're not in dialogue, a window or combat, Exult quicksaves the game in progress. A flat battery costs at most a few minutes. Journey Onward resumes from it.
- **Saves** are in `sdmc:/3ds/exult/saves/bg` and `/si`, the same layout as your PC.
- **Sound:** if `sdmc:/3ds/dspfirm.cdc` is missing, the bottom screen says so at startup.

## Tested in the Azahar emulator (New 3DS mode) with your Black Gate and Serpent Isle data
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

## Fixes from the first hardware run (Oct 25)
- **The Avatar couldn't move.** My default config set Exult's `fps` to 30. In Exult that setting is the game speed, not the display frame rate: 5 is normal, so 30 ran the game six times too fast. Opening Exult's Game Engine options then saved a garbage value, which froze the game timer.
  - The default is now 5.
  - Exult now ignores out-of-range values, and the options screen no longer writes garbage.
  - Your 3DS's `exult.cfg` was corrected over FTP.
- **The log is lighter and timestamped.** Each line of `exult.log` starts with seconds since launch, so slow startup steps show up. The log is now written to the SD card once a second instead of on every line.

## Rebuild from source
Run `build/build.sh`. It needs Docker and uses the `devkitpro/devkitarm` image. The script fetches Exult and SDL3 at the pinned commits, applies `build/patches/`, builds SDL3, libvorbis and Exult for 3DS, and writes `build/out/exult.3dsx`.
- `patches/sdl3-n3ds-virtual-input.patch` turns the 3DS controls and touchscreen into a virtual mouse and keyboard for the top-screen window.
- `3ds/n3ds_platform.cc` handles paths, the default config, the startup legend, the touch hook, logging, and the `glob()` function that newlib lacks.
- `3ds/n3ds_bottom.cc` is the bottom-screen companion: party, dialogue buttons, action bar, touchpad, minimap with tap-to-walk, target cycling and autosave.
- `3ds/n3ds_stereo.cc` handles stereoscopic 3D: the per-pixel height map and the right-eye re-projection.
- `patches/exult-3ds.patch` contains:
  - the hooks;
  - the RAM-backed file loader (fast SD loading);
  - three tiny accessors (conversation state, render target);
  - the 3D hooks (shape painting stamps heights; UI layers are marked as screen plane);
  - crash diagnostics: an uncaught exception or running out of memory is written to `exult.log` instead of the app closing silently;
  - the 3DS touch UI hook.

Exult is GPL v2 (© The Exult Team). The game data isn't included; it comes from your own copy.
