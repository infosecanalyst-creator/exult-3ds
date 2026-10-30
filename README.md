# Exult for New 3DS: Ultima VII on both screens

**Bring your own game files.** This is the Exult engine only; you need your own copy of Ultima VII (the GOG release works).

*Not affiliated with or endorsed by the Exult team, Electronic Arts, Origin or Nintendo. Ultima is a trademark of Electronic Arts, used only to say which game files the engine reads.*

A port of [Exult](https://exult.info), the engine for Ultima VII, to the **New Nintendo 3DS / New 3DS XL / New 2DS XL**. It plays **The Black Gate** (with Forge of Virtue) and **Serpent Isle** (with The Silver Seed), using game data you own (the GOG release works). It's built on the current Exult (1.13.1git) and SDL3's 3DS backend.

The original game was played with a mouse. This port is built for the stylus:

- **You play on the touchscreen.** The game fills it pixel for pixel. Tap where you see, hold the stylus to walk there, and drag items around.
- **The top screen is a dashboard:**
  - your party, a minimap, the time and recent messages;
  - conversations, books, signs, the world map and the party's stats, so nothing covers the game while you read.
- **Readable text:** a pixel font drawn for the 3DS screen replaces the original blackletter and its yellow. It's light grey with a black outline, and conversations get a dark box behind them, like TV subtitles.
- **Stereoscopic 3D:** swap the game onto the top screen and the 3D slider gives the world real depth. Roofs, trees and walls rise out of the screen.
- **A full instruction manual in the game** (L+Y).
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

The full instruction manual is in the game: press **L+Y**, or tap the menu button and then **Manual**. The text is on the top screen, with a button for each section on the touchscreen. Its source is [`build/3ds/manual.txt`](build/3ds/manual.txt), and a `manual.txt` in `sdmc:/3ds/exult/` replaces the built-in one.

### Two screens
- **The touchscreen is the game.** It shows 320×240, pixel for pixel, played with the stylus like a mouse:
  - **tap** clicks;
  - **double-tap** uses something or talks to someone;
  - **hold the stylus still** to walk there;
  - **drag** moves items.
- **The top screen is a dashboard** you never need to touch:
  - party portraits with health and mana bars and status tags (PSN poisoned, FOOD hungry, STRV starving, ZZ asleep, PAR paralyzed, CHM charmed);
  - a minimap of the area, with the party, people and foes marked and the touchscreen's view outlined;
  - the day and time, combat state, what the cursor is on, problems such as "Iolo is hungry", and recent messages.
- **Anything you read appears on the top screen:**
  - **Conversations:** the speaker's portrait and words are on top, and your answers become large buttons on the touchscreen.
  - **Books and scrolls:** shown in the readable font.
  - **Signs, plaques and gravestones:** kept in their rune lettering, with the plain words underneath. Serpent Isle's serpent script stays untranslated.
  - **The world map:** D-Pad up.
  - **The party's stats:** D-Pad down.
- **The menu button** (three bars, top-right of the touchscreen) opens:
  - party portraits (tap one for that inventory);
  - Spells, Notes, Map, Stats, Keys, Food, Target, Combat, Save;
  - the Manual, Swap screens and Game menu.
- **ZL zooms** the touchscreen to 1.5× or 2× around the Avatar. Taps still land where you touch.
- **L+START swaps the screens.** The game goes on top, where the 3D slider works, and the dashboard comes down. Touches then point and click 1:1 on the game above.
- First-time tips appear on the top screen the first time each is relevant.

### Buttons

| Input | Action |
|---|---|
| Circle Pad | Walk |
| C-Stick | Move the cursor |
| A | Left click (hold to drag) |
| B | Hold to walk toward the cursor |
| X | Double click: use / talk |
| Y | Inventory |
| D-Pad ↑ / ↓ | World map / party stats on the top screen |
| D-Pad ← / → | Jump the cursor to the next nearby person or object |
| R | Toggle combat |
| ZL / ZR | Zoom the touchscreen / Target |
| START | Game menu (save, load, options), or close windows |
| SELECT | Quicksave |
| L+START | Swap screens |
| L+Y | Manual |
| Hold L + D-Pad | ↑ Watch · ↓ Pause combat · ← Lockpick · → Food |
| L+SELECT / L+X | Quick-restore / Next music track |
| 3D slider | Stereoscopic depth while the game is on the top screen (off at the bottom) |

Typing (the Avatar's name, save names) uses the 3DS keyboard.

### Stereoscopic 3D

Swap the game onto the top screen with L+START, then push the 3D slider up:
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

- **Game view:** 320×240 at 1:1, the touchscreen's size, which shows a little more of the map than the original 320×200. Exult draws it in the middle of the top screen's 400×240 window, so with the screens swapped the world fills the whole top screen.
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
| `build/3ds/n3ds_bottom.cc` | The two-screen layout: the game on the touchscreen (with zoom and the menu button), the top-screen dashboard, conversations, reading, map, stats, the manual viewer, target cycling, autosave |
| `build/3ds/manual.txt` | The in-game manual (built in via `manual_text.h`; regenerate with `make_manual_h.py`) |
| `build/3ds/n3ds_stereo.cc` | Stereoscopic 3D: height map and right-eye re-projection |
| `build/fonts/` | `u7pixel_glyphs.py` is the hand-drawn pixel font, and `make_fonts.py` builds `fonts_modern.vga` from it and Exult's serif font set. The prebuilt file is included. |
| `build/patches/exult-3ds.patch` | Hooks into Exult: RAM-cached file loading, 3D height stamping, frame and touch hooks, small accessors, fixes |
| `build/patches/sdl3-n3ds-virtual-input.patch` | SDL3's 3DS backend: buttons, sticks and touch become a virtual mouse and keyboard; stereo output; frame and touch hooks for the app |

[TESTING.md](TESTING.md) has the test notes and change history.

## Credits and license

- **Exult:** © the Exult Team, GPL-2.0-or-later. This is an unofficial port; its own files and its changes to Exult (`build/patches/`) are © the exult-3ds authors under the same licence, **GPL-2.0-or-later** (full text in `LICENSE`).
- **SDL3** (zlib licence, patched for the port), **libvorbis** and **libogg** (BSD-3-Clause), **zlib**, **libctru** and **citro3d** (zlib licence) are linked into `exult.3dsx`. Their licence notices are in [`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt), which ships in every release zip with `LICENSE`.
- **Ultima VII:** © Electronic Arts. Its data isn't included and isn't distributed here.
