// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 the exult-3ds authors
// Generated from 3ds/manual.txt (the in-game manual). Regenerate with build/3ds/make_manual_h.py.
#pragma once
static const char* const N3DS_MANUAL_BUILTIN = R"MANUAL(# Welcome
Ultima VII is a huge, open role-playing game. You are the Avatar, back in Britannia two hundred years after your last visit, and a murder in the town of Trinsic is where it begins.

This is Exult, the engine that runs Ultima VII on modern machines, made for the New 3DS. You play on the touchscreen with the stylus, the way the original is played with a mouse. The top screen tells you what you need to know without touching it.

It plays The Black Gate (with Forge of Virtue) and Serpent Isle (with The Silver Seed), using your own copy of the game.

## Reading this manual
- Scroll: Circle Pad or C-Stick up and down. Hold to keep scrolling. You can also drag the stylus up or down.
- Sections: tap one on the touchscreen, or move between them with the D-Pad.
- Previous and next section: L and R.
- B or Start: close. The manual opens again where you left it.

# Getting started
## Installing over Wi-Fi
1. On the 3DS, open the Homebrew Launcher and start ftpd.
2. On the PC, double-click Install-Exult.cmd in the Exult folder. It finds the 3DS, then sends Exult, its data, your game files and any mods.
3. Close ftpd and start Exult from the Homebrew Launcher.

The same steps install every update; only changed files are sent. You never need to take the SD card out.

## The start screen
When Exult starts, the start screen appears right away:
1. Pick The Black Gate or Serpent Isle (if only one is installed, it is picked for you).
2. Continue: the game in progress, where you last stopped (the exit save, the quicksave and the autosave all go there).
3. New Game: the name is Avatar unless you tap it to type another; pick male or female and a skin tone, then Start the game. You begin in Trinsic as the Avatar arrives.
4. Load Game: the saves you named yourself, newest first, by name and date.

The game then loads straight into play. "Exult's own menus" opens Exult's usual menus instead, for the introduction, the credits and mods. "Exit to the Homebrew Launcher" leaves. B goes back a screen where there is one to go back to.

## New Game with a game in progress
A new game takes the place of the game in progress. The start screen asks first ("Start a new game?", the choice starts on NO). If you say YES, the old game is kept: it is written to a new save called "Before new game", and Load Game brings it back. If that save cannot be written (SD card full), no new game is started and the old game carries on.

## Loading
The top screen shows "Loading" with a bar that moves while the game reads the SD card. A new game unpacks its working files to the SD card, which takes a little while. Continue and Load are quicker.

# The two screens
## Bottom screen: the game
The touchscreen shows the game, pixel for pixel. Everything you do happens here, with the stylus or a fingertip.

## Top screen: your dashboard
The top screen never needs touching.
| Party | A portrait for each member, a red health bar and a blue mana bar.
| Tags | PSN poisoned, FOOD hungry, STRV starving, ZZ asleep, PAR paralyzed, CHM charmed.
| Minimap | The land around you. You are white, the party green, people yellow, foes a red X. The box is what the touchscreen shows.
| Info | The day and time, COMBAT while fighting, what the stylus or cursor is on, problems like "Iolo is hungry", and recent messages.
| Alerts | When a party member becomes badly hurt, poisoned or paralyzed, the game says so in its own centred text over the game. Two at once are both shown. There is no alert for how a game loads, for someone who just joined, or for the dead.

The top screen also takes over to show the world map, the party's stats, conversations, books, scrolls and signs.

# Playing with the stylus
| Tap | Click: look at something, pick it up, press it.
| Double-tap | Use something, or talk to someone.
| Hold still | Walk to where the stylus is.
| Drag | Move an item: into a backpack, onto a person, onto the ground.

Small things are easier to hit zoomed in: press L+R (see Zoom, 3D and swapping).

# Buttons
| Circle Pad | Walk
| C-Stick | Move the cursor
| A | Click (hold to drag)
| B | Walk toward the cursor
| X | Use / talk to what the cursor is on
| Y | Inventory
| D-Pad up | World map on the top screen
| D-Pad down | Party stats on the top screen
| D-Pad left/right | Jump the cursor to the next person or object
| R | Combat on / off
| ZL | Swap the screens (any time)
| L+R | Zoom the touchscreen: 1.5x, 2x, normal
| ZR | Target mode
| START | Menu: resume, manual, save / load, game options, commands, left hand, exit game
| SELECT | Quicksave
| L+Y | This manual
| L+SELECT | Quick-restore (asks first)
| L+X | Next music track
| L+D-Pad | Up watch, down pause combat, left lockpick, right food
| 3D slider | Depth, when the game is on the top screen

While the START menu or this manual is open, the buttons and sticks belong to it; nothing reaches the game.

During a conversation, or while a book or sign is open, SAVE / LOAD and GAME OPTIONS in the START menu are greyed ("finish the talk first"): finish, then open them. SELECT does nothing then either.

# The menu button
The three bars in the top-right corner of the touchscreen open the menu.
| Portraits | Tap one to open that member's inventory.
| Spells | The spellbook.
| Notes | Your notebook.
| Map | The world map on the top screen.
| Stats | The party's stats on the top screen.
| Keys | Try your keys on a lock (Keyring mod).
| Food | Feed the party.
| Target | Pick something to use an item on.
| Combat | Combat on or off.
| Save | Save or load.
| Manual | This manual.
| Swap screens | Put the game on the top screen (for 3D) or back.
| Game menu | Exult's menu: save, load, options, quit.
| Left hand | The button at the top of the menu: swaps the D-pad with Y, A, B and X (Y is up, A is right, B is down, X is left, and the reverse), so the stylus can be held in the left hand. It is remembered.

Tap outside it, or press B, to close it.

# Talking and reading
## Conversations
Double-tap someone to talk. Their portrait and words appear on the top screen. Your answers become big buttons along the bottom of the touchscreen: tap one to reply. More... shows the rest when there are many. Between lines, tap the touchscreen to continue.

## Books, scrolls and signs
Their text appears on the top screen in the readable font. Tap the touchscreen for the next page, and again to close.

Signs, plaques and gravestones keep their rune lettering, with the same words in plain letters underneath. Serpent Isle's serpent script stays as it is.

# Zoom, 3D and swapping
## Zoom
L+R zooms the touchscreen to 1.5x, then 2x, then back. The view follows the Avatar and taps still land where you touch. The top screen shows ZOOM while it is on.

## Swapping the screens
ZL (or Swap screens in the commands menu) puts the game back on the top screen and the dashboard on the touchscreen. The game fills the whole top screen, and the touchscreen works like a laptop touchpad: slide to move the cursor, tap to click. Press ZL again to swap back.

## 3D
3D works when the game is on the top screen: swap the screens, then push the 3D slider up. Roofs, trees and people rise out of the screen; windows and text stay flat.

# Saving
There are two kinds of save. The game in progress is one running save: the quicksave, the autosave and the exit save all write to it, and Continue on the start screen picks it up. Named saves are the ones you make yourself; Load Game on the start screen lists them.
- SELECT quicksaves. L+SELECT goes back to the last quicksave; it asks first, because what you did since then is lost.
- START, then SAVE / LOAD (or Save in the menu button's list) keeps named saves.
- The game autosaves every 15 minutes when you are not talking, fighting or in a window. If you are, it tries again half a minute later.
- Continue on the start screen continues the game in progress.
- New Game never throws the game in progress away: it is kept as the save "Before new game".

## Leaving the game
EXIT GAME in the START menu asks "Exit Ultima VII?". YES saves the game in progress first, then leaves; Continue picks it up next time. Closing Exult from the HOME menu (HOME, then Close) does the same.
- Before it saves, the game keeps a copy of your last good game files as gamedat.bak. If the game files are ever damaged, the game starts from that copy.
- It cannot save during a conversation, a fight or with a window open. EXIT GAME then says "Not saved!" right away and asks EXIT ANYWAY or NO. NO takes you back to the game: finish what you were doing and exit again. Closing from HOME in such a moment leaves at once without saving.
- If the save fails (SD card full?) you are asked the same way. The game never leaves after a failed save without asking.
- The settings file (exult.cfg) is written to a temporary file and then renamed, and the old one is kept as exult.cfg.bak, so a failed write cannot lose your settings.

Saves live in sdmc:/3ds/exult/saves and use the same format as Exult on Windows, macOS and Linux. Exult-3DS.cmd option 6 syncs them both ways with your PC.

# Text and fonts
The game uses a pixel font drawn for the 3DS screen, light grey with a black outline, and a dark box behind conversation text. Both can be changed in the game menu: Options, then Display (Fonts and Text background).

# Your game files and mods
Exult needs your own copy of Ultima VII. The installer copies these into sdmc:/3ds/exult/ on the SD card:
| blackgate/ | The Black Gate (plus Forge of Virtue), in its static folder
| serpentisle/ | Serpent Isle (plus The Silver Seed), in its static folder
| .../mods | Mods such as Keyring or SI Fixes

# Troubleshooting
## There is no sound
Homebrew needs a dump of the 3DS sound firmware. Run DSP1 from the Homebrew Launcher once, then restart Exult.

## Starting or saving is slow
Starting a game and saving write many small files to the SD card, which takes a while on the 3DS. Let it finish.

## Something went wrong
Exult keeps a log in sdmc:/3ds/exult/exult.log. Every line is timestamped. It holds the start, the exit and any errors or failed writes. Send it along with a note of what you were doing. For a detailed log (launch timing, the stall watch and Exult's own messages), create an empty file named debug.txt in sdmc:/3ds/exult/ and start the game again; delete it afterwards, as the detailed log slows the game.

## Where everything is
- The program: sdmc:/3ds/exult.3dsx (directly in /3ds).
- Everything else: sdmc:/3ds/exult/ (data, game files, saves, exult.cfg, exult.log).

## Original 3DS or 2DS
Exult needs a New 3DS. On an original 3DS or 2DS it says so and returns to the Homebrew Menu.

## Changing this manual
A file named manual.txt in sdmc:/3ds/exult/ replaces the built-in text. Sections start with "# ", headings with "## ", list items with "- ", numbered steps with "1. ", and table rows with "| ".
)MANUAL";
