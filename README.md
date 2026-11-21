# NBlood-PS3

A native homebrew port of [NBlood](https://github.com/nukeykt/NBlood) for
the PS3, and with it the other games of its source tree: **Blood** (with
Cryptic Passage), **Redneck Rampage** (with Suckin' Grits on Route 66) and
**Redneck Rampage Rides Again** through Rednukem, and **PowerSlave**
through PCExhumed (as PS3Xhumed). Each game is its own package, with its
own XMB entry.

| Game | Package | XMB name | APPID |
|---|---|---|---|
| Blood, Cryptic Passage | `nblood-ps3-1.0.pkg` | NBlood | `NBLOODPS3` |
| Redneck Rampage, Suckin' Grits on Route 66 | `redneck-ps3-1.0.pkg` | Redneck Rampage | `REDNECKRM` |
| Redneck Rampage Rides Again | `redneckra-ps3-1.0.pkg` | Redneck Rampage Rides Again | `REDNECKRA` |
| PowerSlave | `ps3xhumed-1.0.pkg` | PowerSlave Exhumed | `PS3XHUMED` |

## Lineage

NBlood is the reverse-engineered source port of Monolith's Blood, built
on EDuke32's engine technology and Ken Silverman's Build engine. Its tree
also holds Rednukem (Redneck Rampage) and PCExhumed (PowerSlave / Exhumed),
on the same engine. This project adds a full PS3 platform layer underneath
them (video on the RSX, audio, input, system integration), the same one as
[EDuke32-PS3](https://github.com/stingziz0u/EDuke32-PS3). In short:

```
Blood (Monolith, 1997)         -> NBlood    -> NBlood-PS3 (this project)
Redneck Rampage (Xatrix, 1997) -> Rednukem  -> Redneck Rampage on the PS3
PowerSlave (Lobotomy, 1996)    -> PCExhumed -> PS3Xhumed
```

The upstream README, for NBlood itself, is in
[`README.NBlood.md`](README.NBlood.md).

## Common features

What every game here has:

- **A launcher**: at start a menu lists what is installed (skipped when
  there is only one thing to play); Quit from the game's menu comes back
  to it, and so does an error, shown under the list.
- **Video**: Polymost drawn by the RSX, at a stable 60 fps. The internal
  resolution is scaled by the RSX to 720p output. The video options have
  the frame rate limit (60 or 30 fps), an FPS display, widescreen, the
  **Texture Filter** (from sharp to trilinear) and a **TV screen fit**
  (70-100 %) that shrinks the picture for TVs that cut its edges
  (overscan). The filters take much more video memory: if it runs out,
  the filter is turned off by itself and the game says so.
- **Audio**: 48000 Hz on the PS3's audio port. Volumes in steps of 5.
- **Input**: DualShock 3 for movement, look and every action, all
  rebindable from the in-game menus, the sticks too (scale, dead zone,
  saturation, inversion). A stick pushed all the way moves as fast as the
  keys do. An optional **lock-on** (unbound by default): while held, the
  view turns to the nearest enemy in front of you that can be seen and
  follows it; moving and strafing stay free. **Vibration** when you are
  hurt, and when the ground shakes. Only pad number 1 plays: if it powers
  off, it takes over again when it comes back.
- **On-screen keyboard** for the save names.
- **File names in any case**: `BLOOD.RFF`, `blood.rff` and `Blood.rff` are
  all found, even though the PS3's file system tells them apart.
- Clean exit to the XMB from the game's Quit option or the PS button.

## Requirements

- A PS3 capable of running homebrew (HEN or CFW).
- Your own legally-owned copy of each game you want to play. **No game
  data is included in this repository or in any release build** -- see
  each game's Installation.
- To build from source: the [ps3dev / PSL1GHT](https://github.com/ps3dev)
  toolchain (`ppu-gcc`, `spu-gcc`, PSL1GHT SDK, `make_self_npdrm`,
  `pkg.py`).

## Blood

### Tested

**Blood: One Unit Whole Blood** (version 1.21) and **Cryptic Passage**
have been tested on real hardware: whole episodes, their cutscenes and
endings, saving and loading, the demos.

User maps and mods should work the same way, but haven't been tested.

### Features

- **Blood and Cryptic Passage in one package**, each with its own saves in
  `USRDIR/saves/`. Each save has its picture in the Load/Save menus, drawn
  by the software renderer at 320x200 (the GPU's frame can't be read
  back). Saving over a slot replaces it.
- **Load on death**: when you die, the screen offers both: Open restarts
  the level, Fire opens the Load Game menu.
- **Music** through the OPL3 emulation or a **SoundFont** (`.sf2`),
  synthesized on the PS3's SPUs so it costs the game nothing.
- The on-screen texts and the Help page name the DS3's buttons; the
  PC-only settings (keyboard, mouse, display mode) are gone.

### Installation

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

### Controls

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
Options -> Control Setup -> Joystick Buttons Setup, and the sticks from
Joystick Axes Setup.

## Redneck Rampage and Rides Again

### Tested

**Redneck Rampage**, its add-on **Suckin' Grits on Route 66**, and
**Redneck Rampage Rides Again** (the DOS files of GOG's Redneck Rampage
Collection) have been tested on real hardware: playing through levels,
their cutscenes, saving and loading, the CD music.

### Features

- **Two packages**: both games' data is a `REDNECK.GRP`, so each has its
  own install. Suckin' Grits on Route 66 goes with Redneck Rampage, and
  the launcher lists it next to the base game.
- **Separate saves per game** in `USRDIR/saves/`, each with its picture in
  the Load/Save menus.
- **Load on death**: Open restarts the level, Fire loads a saved game;
  the message names the buttons they are on.
- **The CD soundtrack**, as Ogg Vorbis files.
- The on-screen texts name the DS3's buttons; the PC-only settings are
  gone.

### Installation

Install `redneck-ps3-1.0.pkg` (shows up as "Redneck Rampage") and/or
`redneckra-ps3-1.0.pkg` ("Redneck Rampage Rides Again") from the XMB. They
create `/dev_hdd0/game/REDNECKRM/USRDIR/` and
`/dev_hdd0/game/REDNECKRA/USRDIR/`, with empty `addons/` and `music/`
folders (and for Redneck Rampage, `addons/route66/ROUTE66/`). By FTP,
place the files of each game's folder in its own `USRDIR`:

```
REDNECKRM/USRDIR/REDNECK.GRP          <- required, Redneck Rampage's
REDNECKRM/USRDIR/...                  <- the rest of the game's folder
REDNECKRM/USRDIR/music/track02.ogg    <- optional, the CD music, track02 ..
REDNECKRM/USRDIR/addons/route66/      <- optional, Suckin' Grits on Route 66

REDNECKRA/USRDIR/REDNECK.GRP          <- required, Rides Again's
REDNECKRA/USRDIR/...                  <- the rest of the game's folder
REDNECKRA/USRDIR/music/track02.ogg    <- optional, the CD music
```

**Suckin' Grits on Route 66** is loose files read over Redneck Rampage's
`REDNECK.GRP`. In `REDNECKRM/USRDIR/addons/route66/` go its `.CON` files
(`USER66.CON`, and its own `GAME.CON` and the others), its `.ART`
(`TILES009.ART`, `TILES023.ART`), `.VOC`, `.ANM`, `LOOKUP.DAT`,
`PALETTE.DAT` and `TABLES.DAT`; its maps go in
`addons/route66/ROUTE66/`.

**The music**: the CD's audio tracks, encoded as Ogg Vorbis and named
`track02.ogg`, `track03.ogg` and so on (track 1 is the data track), in
the game's `music/` folder.

### Controls

| Action | Button |
|---|---|
| Move / Strafe | Left stick |
| Turn / Look | Right stick |
| Fire | R2 |
| Jump | L2 |
| Open / Use | Cross |
| Use item | Square, Circle |
| Kick | Triangle |
| Crouch | L1 |
| Crouch (toggle) | R3 |
| Alternate fire | R1 |
| Run | L3 |
| Prev / Next weapon | D-pad up / down |
| Prev / Next item | D-pad left / right |
| Map | Select |
| Lock on | *(unbound)* |
| Menu / pause | Start |

In the menus: Cross confirms, Circle goes back. Everything except Start is
rebindable from Options -> Control Setup.

## PowerSlave

### Tested

**PowerSlave** (the original 1996 DOS release) has been tested on real
hardware: the demo, Training, playing through levels, saving and loading,
the CD music. Exhumed (the European release) uses the same `STUFF.DAT`
name and should work the same way, but hasn't been tested.

### Features

- **OPTIONS** in the place of PowerSlave's VOLUME, drawn as the menu's
  other items: game setup (auto run, run button), display (screen size,
  brightness, crosshair, field of view), video mode, sound (sound and
  music on or off, their volumes) and the controller (every button,
  picked from a list; each stick's function, sensitivity, dead zone,
  saturation and inversion; vibration; reset to the defaults). The run
  button's two modes are explained on the screen.
- **The XMB's on-screen keyboard** for the name of a new game's save
  (PowerSlave has no keyboard of its own on the screen).
- **The CD soundtrack**, as Ogg Vorbis files.
- PowerSlave plays with lives: when you die you come back in the same
  level, which keeps its state, as in the original (Training costs no
  lives). So there is no load-on-death screen here.

### Installation

Install `ps3xhumed-1.0.pkg` from the XMB (shows up as "PowerSlave
Exhumed"). This creates `/dev_hdd0/game/PS3XHUMED/USRDIR/` with empty
`addons/` and `music/` folders. By FTP:

```
PS3XHUMED/USRDIR/STUFF.DAT            <- required
PS3XHUMED/USRDIR/BOOK.MOV             <- optional, the intro movie
PS3XHUMED/USRDIR/music/Track02.ogg    <- optional, the CD music, Track02 .. Track19
```

**The music**: the CD's audio tracks, encoded as Ogg Vorbis and named
`Track02.ogg` to `Track19.ogg`, in `music/`. The saves (`savgame*.sav`)
are written in `USRDIR`.

### Controls

| Action | Button |
|---|---|
| Move / Strafe | Left stick |
| Turn / Look | Right stick |
| Fire | R2 |
| Jump | L2 |
| Open / Use | Cross |
| Use item | Square, Circle |
| Center the view | Triangle |
| Crouch | L1, R3 |
| Run | L3 |
| Prev / Next weapon | D-pad up / down |
| Prev / Next item | D-pad left / right |
| Map | Select |
| Lock on | *(unbound)* |
| Menu / pause | Start |

In the menus: the D-pad moves, Cross confirms, Circle or Start goes back.
Everything except Start is rebindable from OPTIONS -> Control Setup.

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

2. Clone this repository and build each game's package:

   ```bash
   git clone https://github.com/stingziz0u/nblood-ps3.git
   cd nblood-ps3
   make -C ps3 -j$(nproc)                  # -> ps3/nblood-ps3-1.0.pkg
   make -C ps3 GAME=rr -j$(nproc)          # -> ps3/redneck-ps3-1.0.pkg
   make -C ps3 GAME=rrra -j$(nproc)        # -> ps3/redneckra-ps3-1.0.pkg
   make -C ps3 GAME=exhumed -j$(nproc)     # -> ps3/ps3xhumed-1.0.pkg
   ```

Each game builds in its own folders, so they don't get in each other's
way. `DEBUG=1` builds a testing package instead (`<name>-debug-1.0.pkg`,
"(debug)" on the XMB): always immortal, every weapon with ammo that
doesn't run out, all the keys, and L3 + R3 together ends the level. It
installs over the game and uses its data; installing the normal package
again goes back.

Blood's SoundFont synthesizer runs on the SPUs and needs `spu-gcc`;
without it, or with `SPU=0`, the SoundFont is synthesized on the PPU
instead.

The `ps3/` directory holds everything specific to this port:

- **`Makefile`** -- builds the engine, the game, the PS3 layer and the SPU
  program, and packages them.
- **`source/`** -- the PS3 layer: `ps3layer.cpp` (the engine's platform
  layer: video modes, the DualShock 3, events, the XMB), `ps3_video.c` (the
  RSX), `gl/` (ps3gl, the OpenGL subset Polymost draws with, on the RSX),
  `driver_ps3.cpp` (the audio port), `ps3_sf2spu.c` (the SoundFont on the
  SPUs), `ps3_launcher.c`, `ps3_osk.c` (the XMB's keyboard), `ps3_stdio.c`
  (files), the log, and `ps3_game_*.cpp` (what the layer asks of each
  game).
- **`spu/`** -- the SoundFont synthesizer's SPU program.
- **`ICON0*.PNG`** -- the XMB icons.

What is PS3-specific in the games themselves is under `#ifdef __PPU__` in
`source/blood/src` (`ps3_blood.cpp` holds the new pieces: the Help page,
the save pictures, the on-screen keyboard's hooks, the death prompt),
`source/rr/src` and `source/exhumed/src` (`ps3options.cpp`: OPTIONS and
the vibration).

### Logs

The release build writes nothing to the HDD but your saves and settings.
For troubleshooting, put a file named `logs.txt` (any content, even empty)
in the game's `USRDIR`: the next start writes the PS3 layer's log there
(`nblood-ps3.log`, `redneck-ps3.log`, `redneckra-ps3.log` or
`ps3xhumed.log`, with fps and memory figures every 10 seconds, the
previous run kept as `.old.log`) and the engine's own log next to it.
Delete `logs.txt` to turn them off again.

## Known limitations

- No multiplayer.
- DualShock 3 only: no USB keyboard or mouse.
- No in-game console (it needs a keyboard).
- Redneck Rampage's art is big: with a texture filter on, the video
  memory can run out, and the filter is turned off.

## Credits

- [NBlood](https://github.com/nukeykt/NBlood) by Nuke.YKT, sirlemonhead,
  NoOne and contributors -- the games this project is built on, Rednukem
  and PCExhumed included.
- [EDuke32](https://www.eduke32.com/) by Richard "TerminX" Gobeille, Evan
  "Hendricks266" Ramos and contributors, for the engine technology NBlood
  uses.
- Ken Silverman, for the Build engine.
- Monolith Productions, for Blood; Xatrix Entertainment, for Redneck
  Rampage; Lobotomy Software, for PowerSlave.
- Bernhard Schelling's TinySoundFont, the SoundFont synthesizer NBlood
  uses (and this port runs on the SPUs).

## License

GPLv2, inherited from NBlood and EDuke32 (see
[`package/common/gpl-2.0.txt`](package/common/gpl-2.0.txt)); the Build
engine under its own license (see
[`package/common/buildlic.txt`](package/common/buildlic.txt)).

No game data files (`.RFF`, `.GRP`, `.DAT`, `.ART`, maps, music, or
otherwise) are included in this repository. You need your own
legally-owned copy of each game.

## AI disclosure

This project's code was written collaboratively with Claude (Anthropic),
working through this port with me in real time over many sessions.
Every bit of testing and debugging were made by me on real hardware.
