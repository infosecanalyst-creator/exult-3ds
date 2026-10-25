# Exult for New 3DS: Ultima VII on both screens

A port of [Exult](https://exult.info), the engine for Ultima VII, to the **New Nintendo 3DS / New 3DS XL / New 2DS XL**. It plays **The Black Gate** (with Forge of Virtue) and **Serpent Isle** (with The Silver Seed), using game data you own (the GOG release works). It's built on the current Exult (1.13.1git) and SDL3's 3DS backend.

The original game was played with a mouse. This port is built for the stylus:

- **The bottom screen shows a live picture of the game.** Tap where you see, hold the stylus to walk there, and drag items around.
- **The bottom screen is also a companion panel:**
  - party portraits with health, mana and status alerts;
  - large dialogue buttons;
  - an action bar;
  - a minimap with tap-to-walk.
- **Readable text:** a pixel font drawn for the 3DS screen replaces the original blackletter and its yellow. It's light grey with a black outline, and conversations get a dark box behind them, like TV subtitles.
- **Stereoscopic 3D:** the 3D slider gives the world real depth. Roofs, trees and walls rise out of the screen.
- **Install and update over Wi-Fi** from a Windows PC. There's no need to take the SD card out, and saves can sync both ways with Exult on the PC.

> Game data isn't included. You need your own copy of Ultima VII.

## Requirements

- **A New 3DS-family console with custom firmware** (Luma3DS and the Homebrew Launcher). The original 3DS/2DS doesn't have enough memory or CPU power.
- **`sdmc:/3ds/dspfirm.cdc` on the SD card** for sound. Run the *DSP1* homebrew once to create it. Without it, the game runs silently and says so at startup.
- **Your Ultima VII data:** the `STATIC` folders of Black Gate and/or Serpent Isle. Mods such as Keyring or SI Fixes are picked up from each game's `mods` folder.
- **For the Wi-Fi installer:** a Windows PC, plus **ftpd** on the 3DS (port 5000, with no username or password set).

## Install

### Over Wi-Fi (recommended)

1. Put the release folder anywhere on your PC.
2. Make sure the installer can find your game data. Either:
   - put the game folders in a `games` folder next to the installer; or
   - leave them where they are, if that's `C:\Ultima`, `C:\GOG Games` or GOG Galaxy's games folder.
   The installer looks for any `STATIC` folder that contains `U7CHUNKS`.
3. Open **ftpd** on the 3DS, then double-click **`Install-Exult.cmd`**.
   - It finds the 3DS on your network and sends Exult, its data, your game data and any mods.
   - After the first time, it only sends files that changed.
   - If a transfer is interrupted, run it again and it carries on where it stopped.
4. Start **Exult** from the Homebrew Launcher.

**`Exult-3DS.cmd`** opens a menu with more options:

| # | What it does |
|---|---|
| 1 | Install / update (same as `Install-Exult.cmd`) |
| 2 | Send Exult and launch it with **3dslink**, with the log streaming to the PC (Homebrew Launcher, press **Y**) |
| 3 | Pull `exult.log` and `exult.cfg` from the 3DS |
| 4 | Send an edited `exult.cfg` back |
| 5 | Re-send everything, for example after reformatting the SD card |
| 6 | Sync saves with Exult on the PC (see below) |

### By SD card

Copy the files so the card looks like this:

```
sdmc:/3ds/exult/exult.3dsx
sdmc:/3ds/exult/data/                  Exult's data files (from the release)
sdmc:/3ds/exult/blackgate/static/      your Black Gate STATIC folder (+ mods/)
sdmc:/3ds/exult/serpentisle/static/    your Serpent Isle STATIC folder (+ mods/)
```

`Copy-U7-To-3DS.ps1` does this for you while the card is in the PC. Exult creates its config and save folders on first launch.

## Playing

### Learning the controls in the game
- **Tips:** the first time you need something, a short tip appears over the party strip. There are tips for touching the picture, portraits, conversations, combat, status tags, zoom, the minimap and 3D. Each one shows once. Tap a tip to close it.
- **The ? button** opens an 8-page illustrated guide. Each page shows a miniature of the bottom screen with the part it explains highlighted. The last page can replay all the tips.
- **Before a game is loaded,** the bottom screen explains how the touch panel works once you're in.

### Bottom screen

- **Party strip (top):**
  - A portrait of each party member, with health (red) and mana (blue) bars.
  - Status tags on the portraits: PSN poisoned, FOOD hungry, STRV starving, ZZ asleep, PAR paralyzed, CHM charmed.
  - The spare space beside the portraits spells out what needs attention, for example "Iolo is hungry", or shows "Party OK".
  - **Tap a portrait** to open that member's inventory.
  - **Drag an item onto a portrait** to give it to that member.
- **Live view (middle):** a copy of the top screen.
  - **Tap** clicks there.
  - **Double-tap** uses something or talks to someone.
  - **Drag** moves items.
  - **Hold the stylus still** to walk to that spot.
  - **Hold L** to zoom 2x around the cursor, for small things like items in a backpack.
- **Info column (right):**
  - The day and time, and COMBAT while you're fighting.
  - What the cursor is on: a person's name with their health, or an item's name and how many there are.
  - The **?** (guide) and **map** buttons.
- **Dialogue:** the middle area turns into large answer buttons.
- **Minimap (map):** terrain around the party, with people marked and the visible area outlined. Tap a spot to walk there.
- **Action bar (bottom):** Map, Spells, Notes, Keys, Pack / Combat, Target, Food, Save, Menu.

### Buttons

| Input | Action |
|---|---|
| Circle Pad | Walk |
| C-Stick | Move the cursor |
| A | Left click (hold to drag) |
| B | Hold to walk toward the cursor |
| X | Double click: use / talk |
| Y | Inventory |
| D-Pad ← / → | Jump the cursor to the next nearby person or object |
| D-Pad ↑ / ↓ | Map / Stats |
| R | Toggle combat |
| ZL / ZR | Try keys (Keyring mod) / Target |
| START | Game menu (save, load, options), or close windows |
| SELECT | Quicksave |
| Hold L + D-Pad | ↑ Watch · ↓ Pause combat · ← Lockpick · → Food |
| L + SELECT / L + Y / L + X | Quick-restore / Help / Next music track |
| 3D slider | Stereoscopic depth (off at the bottom) |

Typing (the Avatar's name, save names) uses the 3DS keyboard.

### Stereoscopic 3D

Push the 3D slider up:
- The ground sits behind the screen.
- People, walls, trees, upper floors and roofs come forward.
- Windows and text stay on the screen plane.

How it works: Exult records the height of every pixel it draws, from each object's lift, rising towards the top of tall shapes. The right-eye image is then rebuilt from those heights. With the slider down, this is switched off and costs nothing.

### Saves, and playing on both PC and 3DS

Saves live in `sdmc:/3ds/exult/saves/bg` and `.../si`. They use the same format as Exult on Windows, macOS and Linux.

Option **6** in `Exult-3DS.cmd` syncs saves both ways with Exult on the PC:
- It reads the PC's save folders from your PC `exult.cfg`. The default location is `C:\Ultima\Config\exult.cfg`; use `-PcExultCfg` to point elsewhere.
- It covers the base games and every mod.
- It **never deletes or silently overwrites a save.** If a slot changed on both machines, both versions are kept, in separate slots.
- If the 3DS has no game in progress yet, it also copies the PC's current game, so **Journey Onward** works straight away.

**Autosave:** every 15 minutes, while you're not in dialogue, in a window or in combat, the game in progress is quicksaved. Journey Onward resumes from it.

### Defaults (`sdmc:/3ds/exult/exult.cfg`)

- **Game view:** 400×240 at 1:1, which fills the top screen and shows more of the map than the original 320×200. If a scene misbehaves in a wide view (the Skara Brae sacrifice is known to), set `video/game` to 320×200.
- **Music and sound:** OPL synth music, mono, 22 kHz. The digital OGG soundtrack is off to save CPU.
- **CPU:** the New 3DS's 804 MHz mode and L2 cache are on.
- **Intro:** the splash intro is skipped. View Introduction is still in the game menu.
- **Fonts:** `modern`. This is U7 Pixel Sans, a font drawn pixel by pixel for this screen: capitals are 9 px tall and lowercase 6 px, in light grey with a black outline. It applies to conversations, names, labels and the bottom screen. Runic and Ophidian writing on signs stays in its original script. You can switch to Exult's Original or Serif fonts in the game's Display options.
- **Text background:** a dark, translucent box behind conversation text (Exult's `textbackground` setting, set to 15, dark grey). Turn it off in Display options.
- **Log:** `sdmc:/3ds/exult/exult.log`, with every line timestamped.

## Building

`build/build.sh` builds everything in Docker using the `devkitpro/devkitarm` image:
- It fetches Exult and SDL3 at pinned commits.
- It applies `build/patches/`.
- It builds SDL3, libvorbis and Exult.
- It writes `build/out/exult.3dsx` and `build/out/data/`.

GitHub Actions runs the same script on every push. Each run uploads a ready-to-copy SD layout as a build artifact.

What's in the source:

| Path | What it is |
|---|---|
| `build/3ds/n3ds_platform.cc` | Heap setup, paths, default config, logging, crash diagnostics, `glob()` |
| `build/3ds/n3ds_bottom.cc` | The bottom-screen companion: live view, party strip, dialogue, action bar, minimap, zoom, drag-to-portrait, target cycling, autosave |
| `build/3ds/n3ds_stereo.cc` | Stereoscopic 3D: height map and right-eye re-projection |
| `build/fonts/` | `u7pixel_glyphs.py` is the hand-drawn pixel font, and `make_fonts.py` builds `fonts_modern.vga` from it and Exult's serif font set. The prebuilt file is included. |
| `build/patches/exult-3ds.patch` | Hooks into Exult: RAM-cached file loading, 3D height stamping, frame and touch hooks, small accessors, fixes |
| `build/patches/sdl3-n3ds-virtual-input.patch` | SDL3's 3DS backend: buttons, sticks and touch become a virtual mouse and keyboard; stereo output; frame and touch hooks for the app |

[TESTING.md](TESTING.md) has the test notes and change history.

## Credits and license

- **Exult:** © the Exult Team, GPL v2. This port is GPL v2 as well (see `LICENSE`).
- **SDL3:** zlib license.
- **Ultima VII:** © Electronic Arts. Its data isn't included and isn't distributed here.
