<p align="center">
  <img src="packaging/windows/theatrhythm-fbl.ico" alt="" width="128">
</p>

<h1 align="center">Theatrhythm Final Bar Line on PC</h1>

<p align="center">
  An unofficial native PC runner for the PS4 version of <b>Theatrhythm Final Bar Line</b>.<br>
  <a href="https://github.com/YAPhoa/theatrhythm-fbl-pc/releases/latest"><b>Download the latest release</b></a>
</p>

It runs the game's own code natively on Windows, with a Vulkan renderer, keyboard and gamepad
controls, and display settings of its own. **No game files are included:** you need a decrypted
dump of your own copy of the game (PS4, version 1.00).

## Features

- **PC display settings:** windowed, borderless or fullscreen, at your desktop's resolution.
- **Keyboard and controller:** Xbox-style mapping for controllers, with the game's button hints
  redrawn for keyboard and Xbox. DualSense works too.
- **Easy setup:** point the installer at your dump, and it can add a desktop shortcut.

## Supported languages

All of the game's own languages are available (it asks which one on first boot). English is the
one played most with this runner so far.

- ✅ English
- ✅ Japanese
- ✅ French
- ✅ German
- ✅ Italian
- ✅ Spanish
- ✅ Korean
- ✅ Chinese (Traditional)
- ✅ Chinese (Simplified)

## Download

Download `theatrhythm-fbl-pc-<version>.zip` from the
[latest release](https://github.com/YAPhoa/theatrhythm-fbl-pc/releases/latest).

## Requirements

- Windows 10 or 11, 64-bit
- A CPU with AVX
- A GPU with Vulkan 1.3 and an up-to-date driver
- A decrypted dump of your own copy of the game: the folder with `eboot.bin`, `sce_module`,
  `sce_sys` and `Media`

Python and other tools come with the release: you don't need to install anything else.

### Tested on

| Part | Model |
|---|---|
| CPU | AMD Ryzen 7 9800X3D |
| GPU | NVIDIA GeForce RTX 5080 |
| RAM | 64 GB |
| OS | Windows 11 Pro |

That's far more than the game needs. It was made for a PS4, and the runner adds little on top, so
a much more modest PC should play it fine: any reasonably recent CPU with AVX and a GPU with a
current Vulkan 1.3 driver. Lower-end machines haven't been tested yet, so if yours works (or
doesn't), please say so in an [issue](https://github.com/YAPhoa/theatrhythm-fbl-pc/issues).

## Install and play

1. Unzip the release anywhere, for example `C:\Games\Theatrhythm`.
2. Run `theatrhythm-setup.exe`.
3. Choose your **game folder** (the dump) and a **save folder** (by default `save` inside the
   release folder), then press **Install**. Setup prepares the game from your dump in a few
   seconds and offers a desktop shortcut. Your dump is only read, never changed.
4. Press **Play Game**. Later, start `theatrhythm-fbl.exe` or the shortcut.

Run `theatrhythm-setup.exe` again any time to change the game or save folder. It offers to copy your
saves when the save folder moves.

### Updating

Unzip the new release over the old folder, then run `theatrhythm-setup.exe` once more (the game
asks you to). Your folders, saves and settings are kept.

## Settings

Setup writes `settings.ini` next to the game. It holds the window mode and size, monitor, vsync,
frame limit, audio volume and speaker layout, vibration, the dialog confirm button and control
remapping, each with a comment explaining it.

In the game, **Options > Display Settings** (an extra button below Streaming Rules) changes the
window mode, resolution, vsync and frame rate limit. Changes apply at once and are saved to
`settings.ini` when you leave the page.

## Controls

The game sees one DualShock 4. Any gamepad works, mapped by button position: on an Xbox pad A B X Y
are Cross, Circle, Square, Triangle; LB/RB are L1/R1, LT/RT are L2/R2, Menu is Options and View
clicks the touchpad. The keyboard and mouse work alongside a gamepad. In rhythm play any button
taps a trigger and either stick slides, so each hand gets a stick and taps:

| Keys | PS4 |
|---|---|
| W A S D / I J K L | left stick / right stick |
| arrow keys | d-pad (menus) |
| Space, Enter | Cross |
| Escape, Backspace, `;` | Circle |
| F / H | Square / Triangle |
| Q, Page Up / O, Page Down | L1 / R1 |
| E / U | L2 / R2 |
| X / M | L3 / R3 |
| Tab, P | Options |
| T | touchpad click |

Keys are physical positions, so they're the same on AZERTY or QWERTZ keyboards. To change them,
edit `key_map` and `pad_map` in `settings.ini`, e.g. `key_map=cross=G,cross=Mouse Left`: the first
mention of a button replaces its defaults, the next ones add more, and `=None` unbinds it.

On-screen button prompts follow the device you last used: PlayStation icons for a DualShock or
DualSense, Xbox icons for other gamepads, and key names for the keyboard. Set `button_icons` in
`settings.ini` to `ps4`, `xbox` or `keyboard` to keep one set.

## Issues and suggestions

Found a problem? [Open an issue](https://github.com/YAPhoa/theatrhythm-fbl-pc/issues) and attach
`theatrhythm.log` from the `log` folder in your save folder. Any suggestion, just open an issue too.

## Building from source

See [docs/building.md](docs/building.md) for the toolchain, building, tests and packaging, and
[docs/development.md](docs/development.md) for how the runner works and how to work on it. Every
push is built and tested by GitHub Actions, and tagging a version publishes a release.

## Credits and license

GPL-2.0-or-later, see [LICENSE](LICENSE). The loader, runtime and GPU shim are derived from
SpartanPort by Felipe Prado and contributors (GPL-2.0). The video core and parts of the runtime
follow [shadPS4](https://github.com/shadps4-emu/shadPS4). Theatrhythm Final Bar Line is a trademark
of Square Enix. This project is not affiliated with or endorsed by Square Enix or Sony Interactive
Entertainment.
