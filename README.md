# NBlood-PS3

A native homebrew port of [NBlood](https://github.com/nukeykt/NBlood) for
the PS3: Blood, with Cryptic Passage, in one package.

## Lineage

NBlood is the reverse-engineered source port of Monolith's Blood, built
on EDuke32's engine technology and Ken Silverman's Build engine. This
project adds a full PS3 platform layer underneath it (video on the RSX,
audio, input, system integration), the same one as
[EDuke32-PS3](https://github.com/stingziz0u/EDuke32-PS3). In short:

```
Blood (Monolith, 1997) -> NBlood -> NBlood-PS3 (this project)
```

The upstream README, for NBlood itself, is in
[`README.NBlood.md`](README.NBlood.md).

## Tested games

**Blood: One Unit Whole Blood** (version 1.21) and **Cryptic Passage**
have been tested on real hardware: whole episodes, their cutscenes and
endings, saving and loading, the demos.

User maps and mods should work the same way, but haven't been tested.

## Features

- **One package, one launcher**: Blood and Cryptic Passage in a single
  install. At start a menu lists what is installed (skipped when there is
  only one thing to play); Quit from the game's menu comes back to it, and
  so does an error, shown under the list.
- **Separate saves per game**: Blood and Cryptic Passage keep their own
  saves in `USRDIR/saves/`. Each save has its picture in the Load/Save
  menus, drawn by the software renderer at 320x200 (the GPU's frame can't
  be read back). Saving over a slot replaces it.
- **On-screen keyboard** for the save names and the player name.
- **Load on death**: when you die, the screen offers both: Open restarts
  the level, Fire opens the Load Game menu.
- **Video**: Polymost drawn by the RSX, at a stable 60 fps. The internal
  resolution (up to 1280x720) is scaled by the RSX to 720p output.
  Options -> Display Setup -> Video Mode has the frame rate limit (60 or
  30 fps), an FPS display, widescreen, the **Texture Filter** (from sharp
  to trilinear) and a **TV screen fit** (70-100 %) that shrinks the
  picture for TVs that cut its edges (overscan).
- **Audio**: 48000 Hz on the PS3's audio port. The music through the OPL3
  emulation or a **SoundFont** (`.sf2`), synthesized on an SPU so it costs
  the game nothing. Volume sliders in steps of 5.
- **Input**: DualShock 3 for movement, look and every action, all
  rebindable from the in-game menus, plus an optional **lock-on** (unbound
  by default) and vibration. The PC-only settings (keyboard, mouse,
  display mode) are gone, and the on-screen texts and the Help page name
  the DS3's buttons. Only pad number 1 plays: if it powers off, it takes
  over again when it comes back.
- **File names in any case**: `BLOOD.RFF`, `blood.rff` and `Blood.rff` are
  all found, even though the PS3's file system tells them apart.
- Clean exit to the XMB from the game's Quit option or the PS button.

## Requirements

- A PS3 capable of running homebrew (HEN or CFW).
- Your own legally-owned copy of Blood. **No game data is included in this
  repository or in any release build** -- see
  [Installation](#installation).
- To build from source: the [ps3dev / PSL1GHT](https://github.com/ps3dev)
  toolchain (`ppu-gcc`, `spu-gcc`, PSL1GHT SDK, `make_self_npdrm`,
  `pkg.py`).

## Installation

Install `nblood-ps3-1.0.pkg` from the XMB (shows up as "NBlood"). This
creates `/dev_hdd0/game/NBLOODPS3/USRDIR/` with an empty `addons/` folder
and NBlood's own `nblood.pk3`. By FTP, place the files of your Blood 1.21
there:

```
NBLOODPS3/USRDIR/BLOOD.INI            <- required
NBLOODPS3/USRDIR/BLOOD.RFF            <- required
NBLOODPS3/USRDIR/GUI.RFF              <- required
NBLOODPS3/USRDIR/SOUNDS.RFF           <- required
NBLOODPS3/USRDIR/SURFACE.DAT          <- required
NBLOODPS3/USRDIR/VOXEL.DAT            <- required
NBLOODPS3/USRDIR/TILES000.ART         <- required, TILES000 .. TILES017
...
NBLOODPS3/USRDIR/BLOOD000.DEM         <- optional, the demos (BLOOD000 .. 003)
NBLOODPS3/USRDIR/*.SMK                <- optional, the cutscenes
NBLOODPS3/USRDIR/CRYPTIC.INI          <- optional, Cryptic Passage, with its
NBLOODPS3/USRDIR/CP*.MAP                 maps, CPART07.AR_ and CPART15.AR_
NBLOODPS3/USRDIR/CPART07.AR_
NBLOODPS3/USRDIR/GeneralUser-GS.sf2   <- optional, any SoundFont for the music
```

The GOG and Steam releases (Blood: One Unit Whole Blood) have all of
these. The demos have to be the ones of version 1.21: older ones are
skipped.

For the music, the first `.sf2` in `USRDIR` is used, and Options -> Sound
Setup -> SF2 Bank picks another one. A SoundFont stays in memory while the
game runs (GeneralUser-GS takes 33 MB); one that would leave the game too
little memory isn't loaded, and the music goes to the OPL3 emulation.
SoundFonts of 5-30 MB are the comfortable range.

## Building from source

1. Install the [ps3dev toolchain](https://github.com/ps3dev/ps3toolchain)
   (third-party, not part of this project -- these are its own official
   instructions):

   ```bash
   git clone https://github.com/ps3dev/ps3toolchain.git
   cd ps3toolchain
   export PS3DEV=/usr/local/ps3dev
   export PSL1GHT=$PS3DEV
   export PATH="$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin"
   ./toolchain.sh
   ```

2. Clone this repository and build:

   ```bash
   git clone https://github.com/stingziz0u/NBlood-PS3.git
   cd NBlood-PS3
   make -C ps3 -j$(nproc)            # -> ps3/nblood-ps3-1.0.pkg
   ```

The SoundFont synthesizer's SPU program needs `spu-gcc`; without it, or
with `SPU=0`, the SoundFont is synthesized on the PPU instead.

The `ps3/` directory holds everything specific to this port:

- **`Makefile`** -- builds the engine, the game, the PS3 layer and the SPU
  program, and packages them.
- **`source/`** -- the PS3 layer: `ps3layer.cpp` (the engine's platform
  layer: video modes, the DualShock 3, events, the XMB), `ps3_video.c` (the
  RSX), `gl/` (ps3gl, the OpenGL subset Polymost draws with, on the RSX),
  `driver_ps3.cpp` (the audio port), `ps3_sf2spu.c` (the SoundFont on an
  SPU), `ps3_launcher.c`, `ps3_stdio.c` (files), the log.
- **`spu/`** -- the SoundFont synthesizer's SPU program.
- **`ICON0.PNG`** -- the XMB icon.

What is PS3-specific in the game itself is under `#ifdef __PPU__` in
`source/blood/src` (`ps3_blood.cpp` holds the new pieces: the Help page,
the save pictures, the on-screen keyboard's hooks, the death prompt).

### Logs

The release build writes nothing to the HDD but your saves and settings.
For troubleshooting, put a file named `logs.txt` (any content, even empty)
in `USRDIR`: the next start writes `nblood-ps3.log` (the PS3 layer, with
fps and memory figures every 10 seconds, the previous run kept as
`nblood-ps3.old.log`) and the engine's own `nblood.log` next to it.
Delete `logs.txt` to turn them off again.

## Controls

| Action | Button |
|---|---|
| Move / Strafe | Left stick |
| Turn / Look | Right stick |
| Fire | R2 |
| Alternate fire | L2 |
| Jump | Cross |
| Crouch (toggle) | Circle |
| Open / Use | Square |
| Last weapon | Triangle, D-pad up |
| Prev / Next weapon | L1 / R1 |
| Use item | D-pad down |
| Prev / Next item | D-pad left / right |
| Turn around | R3 |
| Map | Select |
| Lock on | *(unbound)* |
| Menu / pause | Start |

In the menus: Cross confirms, Circle goes back, Start closes them; holding
a direction repeats it. Everything except Start is rebindable from
Options -> Control Setup -> Joystick Buttons Setup, and the sticks (scale,
dead zone, saturation, inversion) from Joystick Axes Setup. **Lock on**,
while held, turns the view to the nearest enemy in front of you that can
be seen and follows it; moving and strafing stay free.

## Known limitations

- No multiplayer.
- DualShock 3 only: no USB keyboard or mouse.
- No in-game console (it needs a keyboard).

## Credits

- [NBlood](https://github.com/nukeykt/NBlood) by Nuke.YKT, sirlemonhead,
  NoOne and contributors -- the game this project is built on.
- [EDuke32](https://www.eduke32.com/) by Richard "TerminX" Gobeille, Evan
  "Hendricks266" Ramos and contributors, for the engine technology NBlood
  uses.
- Ken Silverman, for the Build engine.
- Monolith Productions, for Blood.
- Bernhard Schelling's TinySoundFont, the SoundFont synthesizer NBlood
  uses (and this port runs on an SPU).

## License

GPLv2, inherited from NBlood and EDuke32 (see
[`package/common/gpl-2.0.txt`](package/common/gpl-2.0.txt)); the Build
engine under its own license (see
[`package/common/buildlic.txt`](package/common/buildlic.txt)).

No game data files (`.RFF`, `.ART`, maps, music, or otherwise) are
included in this repository. You need your own legally-owned copy of the
game.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions.
Every bit of testing and debugging were made by me on real hardware.
