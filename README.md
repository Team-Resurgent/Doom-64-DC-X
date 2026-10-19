# Doom 64 for the original Xbox

A native original-Xbox port of *Doom 64* (N64, 1997), built from
[doom64-dc](https://github.com/jnmartin84/doom64-dc), jnmartin84's Dreamcast
port, the same way as the Super Smash Bros., Star Fox 64, Ocarina of Time
and Mario Kart 64 Xbox ports: the Dreamcast port's game code is compiled for
the Xbox as it is, and the slice of KallistiOS it runs on -- the PowerVR, the
AICA sound chip, the maple bus, the memory card, threads, files, timers, the
SH-4's matrix unit -- is reimplemented on the Xbox's own hardware in
`Platform/xbox/`.

**No game data is included.** You need your own Doom 64 ROM: the **US v1.0
or v1.1** release (`.z64`, or a byte-swapped `.v64` / `.n64`, which the
build puts in order). Everything taken from it is made on your own machine.
Optionally, the IWAD from the 2020 Nightdive release (`doom64.wad`) adds the
Lost Levels, as on the Dreamcast.

| Version | MD5 (`.z64`) |
| --- | --- |
| US v1.0 | `b67748b64a2cc7efd2f3ad4504561e0e` |
| US v1.1 | `1b1378bb9ee819f740550f566745af73` |

## Features

Everything the Dreamcast port has: the whole game and its passwords, the
Knee-Deep-in-the-Dead bonus episode, the Lost Levels (with the IWAD), smooth
motion interpolation and an uncapped frame rate (60 fps on the Xbox, its
display's rate), dynamic coloured lighting, and real-time normal mapping on
the world and the weapons (Options > Video > Quality: Ultra, the default) --
the PVR's bump-map shading, done on the Xbox's NV2A with its DOT3 texture
stages.

* 720p and 480p, following the dashboard (the 4:3 picture, black bars on a
  16:9 screen)
* Saves, settings and your button layout kept on the Xbox's hard disk
  (dashboard > Memory > Doom 64)
* Buttons remappable in game (Options > Gamepad)
* Rumble (Options > Movement > Rumble: On)

## Requirements

* Windows 10 or 11
* the RXDK toolchain: in Visual Studio, **RXDK > RXDK Window > Install
  Prerequisites** (or RXDK's own installer), in `C:\ProgramData\RXDK`
* [Python](https://www.python.org/downloads/) 3.8 or later
* [Git](https://git-scm.com/download/win)
* an internet connection the first time you build: the Dreamcast port's own
  data -- its music (about 600 MB), sound effects, normal maps and bonus
  levels -- is fetched from its repository

Nothing else -- no CMake, Make, MSYS or Docker: the ROM extractor
(`wadtool/`) is compiled directly with the RXDK's own clang (or Visual
Studio's `cl`, or a `gcc` on the `PATH`), so the version of any CMake you
have installed does not matter.

`python setup.py --check` lists what is there and what is missing.

## Building

1. Download this repository (**Code > Download ZIP**, or `git clone`).
2. Put your ROM in its folder, named `baserom.z64` (`doom64.z64` works too).
   For the Lost Levels, put the 2020 release's `DOOM64.WAD` beside it as
   `doom64.wad`.
3. Double-click **`build.bat`**, or in a console:

```bash
python setup.py
```

The result is **`out\Release\XISO\Doom-64-DC-X.iso`** (about 615 MB).

The build runs, in order:

1. checks the ROM (MD5) and writes it in `.z64` order to `build/`;
2. fetches [doom64-dc](https://github.com/jnmartin84/doom64-dc) at the revision
   `setup.py` pins (`DOOM64_DC_REV`, the one `src/` and `wadtool/` were taken
   from) into `extern/doom64-dc`, for its `selfboot/` data;
3. builds `wadtool`, the Dreamcast port's ROM extractor, for Windows, and runs
   it on the ROM (and the IWAD): the game's WADs, its 33 levels and its
   sprite sheet, into `build/wadgen/`;
4. stages `data/`: the Dreamcast disc's files, which the game reads as `/cd`
   and the Xbox as `D:\data`;
5. makes the dashboard icons from the Dreamcast port's cover art, and writes
   `rxdk.project.json` and the Visual Studio project;
6. builds with `Rxdk.Cli` and packs the ISO.

```bash
python setup.py --check         # what is present and what is missing
python setup.py --skip-build    # everything but the compile, e.g. to build in Visual Studio
python setup.py --config Debug  # the debug configuration
```

### In Visual Studio

Run `python setup.py --skip-build` once, open `Doom-64-DC-X.sln`, choose the
**Xbox** platform and Build. **F5** runs it on your devkit with the Xbox
Debugger (RXDK). The build lands where the command-line one does:
`out\<Configuration>\XISO\Doom-64-DC-X.iso`, and the disc's files in
`out\<Configuration>\Build\Doom-64-DC-X\`.

## Running

* **Disc image:** boot `out\Release\XISO\Doom-64-DC-X.iso` on a modded console
  or in [xemu](https://xemu.app/).
* **Hard disk:** copy the folder `out\Release\Build\Doom-64-DC-X\` (the
  `default.xbe` and its `data` folder) to your Xbox and launch `default.xbe`.

## Controls

### In game (the defaults)

| Xbox | Action |
| --- | --- |
| Left stick | walk forward / back, strafe |
| Right stick | turn |
| Right trigger | fire |
| Left trigger | walk slowly while held (with Always Run on, the default) |
| A, or click the right stick | use: open doors, press switches |
| BACK | automap |
| X, or WHITE | previous weapon |
| Y, or BLACK | next weapon |
| D-pad | walk and turn |
| START | pause menu |
| B | nothing (it is the menus' back button; give it an action in Options > Gamepad) |

On the automap, hold A to move around the map: the left stick pans it and
the triggers zoom.

### In the menus

* Left stick or D-pad: move, and change a setting
* **A**: select
* **B** or **BACK**: back
* **START**: on the pause menu, close it and play on; in any other menu it
  does nothing

### Changing the buttons

Options > Gamepad lists every action and its buttons. On an action:

* **A**, then press a button: that button does the action
* **Y**, then press a button: adds it as a second button (either one works)
* **X**: clears the action
* **START** cancels while it waits for a button

A button does one thing: giving it to one action takes it off another.
**Reset to Defaults**, the top row, puts back the layout above.

Your layout is saved when you leave the screen, in the game's save on the
Xbox (dashboard > Memory > Doom 64), so deleting that save resets it too.

What the actions do: **speed** runs while held (walks while held with
Always Run on); **strafe on** makes the turning stick and the D-pad's
left / right strafe while held; **strafe left** / **strafe right** strafe.
They start with no buttons: the left stick already strafes.

### Saving and loading

Doom 64 saves between levels: after each level's tally screen comes the
save screen -- pick a slot and press **A** (or **B** to skip); saving over a
slot replaces it. **Load Game** on the title and pause menus lists the
saves. **Password** goes straight to entering a level password.

Holding START while the game starts lists its files, and holding **Y** and
pressing **A** there deletes one: `DOOM64` is all the save slots,
`DOOM64STG` the settings and `DOOM64CTL` the button layout. (Deleting the
game's save in the dashboard does the same for all three.)

### Optional: controls.ini

None is included, and none is needed. For a starting layout of your own
without using the Gamepad screen, put a `controls.ini` in the `data\`
folder beside `default.xbe` (a layout saved from the Gamepad screen still
wins over it; Reset to Defaults goes back to it). One line per action, in
this order, with 0, 1 or 2 buttons; two buttons means either one. This is
the built-in layout (all 14 lines are needed, the last one a Dreamcast
keyboard setting -- a file that is not exactly right is ignored, and the
built-in layout used):

```
RIGHT,1,DPAD_RIGHT
LEFT,1,DPAD_LEFT
UP,1,DPAD_UP
DOWN,1,DPAD_DOWN
ATTACK,1,TRIGGER_R
USE,2,BUTTON_A,THUMB_R
AUTOMAP,1,BUTTON_BACK
SPEED,1,TRIGGER_L
STRAFE,0
STRAFELEFT,0
STRAFERIGHT,0
WEAPONBACKWARD,2,BUTTON_X,BUTTON_WHITE
WEAPONFORWARD,2,BUTTON_Y,BUTTON_BLACK
KEYBSWAPSTRAFE,0
```

Buttons: `BUTTON_A`, `BUTTON_B`, `BUTTON_X`, `BUTTON_Y`, `BUTTON_WHITE`,
`BUTTON_BLACK`, `BUTTON_BACK`, `TRIGGER_L`, `TRIGGER_R`, `THUMB_L`, `THUMB_R`
(the stick clicks), `DPAD_UP`, `DPAD_DOWN`, `DPAD_LEFT`, `DPAD_RIGHT`. No
spaces anywhere in the file. (It is the Dreamcast port's format,
[README.dreamcast.md](README.dreamcast.md), with the Xbox pad's extra
buttons; on the Dreamcast two buttons mean both at once.)

## How it maps onto the Xbox

| Dreamcast | Xbox (`Platform/xbox/`) |
| --- | --- |
| PowerVR (KOS `pvr_*`) | `pvr_xbox.c`: Direct3D 8 on the NV2A. Texture and palette memory are kept as on the PVR and untwiddled into NV2A textures when a header names them. The opaque list goes straight to it, the punch-through and translucent lists are read from the game's own DMA vertex buffers when the scene finishes; the translucent list is sorted far to near (the PVR's autosort). Sprites, the 128-entry fog table, the secondary accumulation buffer, and bump mapping: a bump texture becomes a normal map and the PVR's `K1 + K2 sin S + K3 cos S cos(R-Q)` is evaluated as `K1 + N.L` in four texture stages. 1/w depth in a floating-point depth buffer. |
| AICA (KOS `snd_*`, `spu_*`) | `aica/`: sound RAM, the channel registers and the command queue, KOS's stock ARM firmware on the x86, Yamaha ADPCM / PCM mixed in software into a DirectSound stream; `kos_snd_stream_xbox.c` (the music) and `kos_sfxmgr_xbox.c` (the sound effects) are KOS's own. |
| maple bus | `kos_maple_xbox.c`: XInput pads and their rumble motors. |
| VMU | `kos_vmu_xbox.c` + `kos_vmu_pkg.c`: a VMU whose files live in the title's save game on `U:`. |
| `/cd` | `kos_fs_xbox.c`: `D:\data`. |
| SH-4 matrix unit, threads, timers | `kos_doom_xbox.c`, `kos_thread_xbox.c`, `kos_sys_xbox.c`, `kos_extra_xbox.c` |

The changes to the Dreamcast port's own files are few and each is marked
`XBOX:`:

* `src/i_main.c` -- the entry point's name, the PT alpha register, the
  system ticker's pacing, the Xbox pad's layout in game and in the menus
  (`I_XboxPadData`), remapping and its save file, the rumble packets, no
  raised IRQL around the screen wipes' frame reads;
* `src/m_main.c` -- the Gamepad screen's remapping, Load Game apart from
  Password, START only on the pause menu, the prompts and the "VMU" wording
  in the Xbox pad's and the hard disk's terms, no VMU Display option, the
  rumble option's name;
* `src/doomdef.h`, `src/m_password.c`, `src/d_screens.c`, `src/p_tick.c` --
  the same, where those screens and menus live;
* `src/p_user.c` -- the stick walks at the run setting's speed (on the N64
  it always runs);
* `wadtool/wadtool.c` -- an uninitialised palette pointer (read for every
  weapon frame after the first; GCC's code happened to survive it, clang's
  does not).

## Layout

```
build.bat                  double-click build: finds Python, then runs setup.py
setup.py                   the one-command build
rxdk.project.json          the build manifest (tools/xbox/gen_manifest.py)
Doom-64-DC-X.sln/.vcxproj  Visual Studio: a native RXDK project (tools/xbox/gen_vcxproj.py)
Platform/xbox/             the Xbox platform layer (KallistiOS on the Xbox)
tools/xbox/                host-side build glue (config, manifest, icons, checks)
src/                       the Dreamcast port's game code
wadtool/                   its ROM extractor
extern/                    the Dreamcast port at its pinned revision (fetched, not committed)
build/                     wadtool and what it extracts (not committed)
data/                      the disc's data folder (generated, not committed)
```

## Licensing

See [LICENSE](LICENSE) and [NOTICE](NOTICE). No ROM, IWAD, music, sound or
other game data is in this repository, and `.gitignore` keeps everything
`setup.py` fetches or makes out of it. Doom 64 is still sold and is active
intellectual property: a disc image built from it is for your own use, not
for redistribution.

## Credits

* [doom64-dc](https://github.com/jnmartin84/doom64-dc) by jnmartin84 and its
  contributors, the Dreamcast port this is built from (see
  [README.dreamcast.md](README.dreamcast.md) for its own credits)
* [DOOM64-RE](https://github.com/Erick194/DOOM64-RE) by Erick194, and
  Immorpher's [Doom 64 XE](https://github.com/Immorpher/doom64xe)
* [KallistiOS](https://github.com/KallistiOS/KallistiOS), whose API this reimplements
* Team Resurgent's RXDK
