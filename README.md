# Halo Crossing

Halo: Combat Evolved's combat, inside your Animal Crossing (GameCube) town.
You play as the Master Chief in first person, with Halo's weapons, grenades,
shields and HUD, while Covenant squads invade the village. Villagers run and
hide. Tom Nook can be shot, and in one scenario he chases you around his
shop yelling about the money you owe him.

It runs on [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port), a native
Windows port of Animal Crossing built from the
[ac-decomp](https://github.com/ACreTeam/ac-decomp) decompilation. Halo's
sandbox (movement, weapons, AI, damage) is a new implementation in C. When you
supply your own Halo disc, its models, animations, sounds and HUD are
converted on your machine and drawn in the game.

This is an unofficial, non-commercial fan prototype. It is not affiliated
with or endorsed by Nintendo, Microsoft, Xbox Game Studios, Bungie or 343
Industries. Animal Crossing is a trademark of Nintendo. Halo is a trademark
of Microsoft.

## No game data is included

This repository holds only original source code, build scripts, an asset
converter, and a patch that adds hook calls to the AC port. It does not
contain, and will never host or link to:

* disc images, ROMs, BIOS files or game executables
* models, textures, animations, sounds, music, text or other game assets
* save files

**You need your own copies of both games.** The build reads your Animal
Crossing disc image in place. The importer reads your Halo disc image and
writes converted files into `assets_local/`, which Git ignores. Nothing from
either disc ends up in the repository, and the converted files must not be
shared or uploaded. Requests for game files will be closed.

The AC port is a separate project, pulled in as a Git submodule from its own
repository. It also ships no game assets.

## What you need

| | |
|---|---|
| PC | Windows 10 or 11, 64-bit. A GPU with OpenGL 3.3. |
| Animal Crossing | A disc image of the **USA** GameCube release (game ID `GAFE01`, revision 0), made from a disc you own (for example with CleanRip on a Wii). `.iso`, `.gcm` and `.ciso` work. For `.rvz`, convert it first in Dolphin: right-click the game, choose **Convert File...**, and pick ISO. |
| Halo: Combat Evolved | Optional but strongly recommended. A disc image of the **Xbox** release made from a disc you own, as an XISO or a full ISO. Tested with the USA release. Without it the game still runs, but the Chief, his weapons and the Covenant are placeholder boxes, sounds are beeps, and the HUD is a simple one. |
| Disk space | About 200 MB for the code, build and converted packs, plus your disc images. |

## 1. Install the tools

1. Install [MSYS2](https://www.msys2.org/) to the default `C:\msys64`. If you
   install it somewhere else, set an `MSYS2_ROOT` environment variable to
   that folder before building.
2. Open **MSYS2 MINGW32** from the Start menu. Make sure it's MINGW32, not
   UCRT64 or MSYS. Then run:
   ```
   pacman -Syu
   pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja mingw-w64-i686-SDL2 mingw-w64-i686-python
   ```
   If `pacman -Syu` closes the window, reopen MINGW32 and run it again
   before the second command.
3. Install [Python 3](https://www.python.org/downloads/) for Windows, ticking
   **Add python.exe to PATH**. Then, in PowerShell or Command Prompt:
   ```
   pip install numpy
   ```
   This is only needed for the Halo importer.
4. Install [Git for Windows](https://git-scm.com/download/win) so you can
   clone from PowerShell. The build can use either this or MSYS2's `git`.

## 2. Get the code

In PowerShell:

```
git clone --recursive https://github.com/AdamMcWilliam/halo-crossing.git
cd halo-crossing
```

`--recursive` also fetches the AC port into `external/ACGC-PC-Port`. If you
forgot it, the build fetches it for you.

All commands below are run from this `halo-crossing` folder.

## 3. Add your Animal Crossing disc image

```
mkdir assets_local\animal_crossing
copy "D:\path\to\your\Animal Crossing.iso" assets_local\animal_crossing\
python tools\check_disc.py
```

The check should print your image's name followed by
`GAFE01 rev 0 'AnimalCrossing' -> ok`. `UNSUPPORTED` means it isn't the USA
release, and the port won't run it.

## 4. Convert your Halo assets (optional)

```
python tools\import_halo.py --iso "D:\path\to\your\Halo - Combat Evolved.iso"
```

This reads the image in place and takes about 10 seconds. When it finishes,
`assets_local\halo\generated\` contains four files: `fp_weapons.hcpk`,
`bipeds.hcpk`, `sounds.hcpk` and `hud.hcpk`, about 32 MB in total. These are
converted from your disc. Keep them to yourself.

If you update the code later, run the importer again. The game ignores packs
written by an older importer and falls back to the placeholders.

## 5. Build

```
.\build.bat
```

The first build compiles about 4,000 files: under two minutes on a fast PC,
longer on a slower one. Later builds only recompile what changed. `build.bat`
runs `build.sh` in MSYS2's MINGW32 environment. It applies `patches/host/` to the AC port,
builds `build\host\bin\AnimalCrossing.exe`, and links your disc image into
`build\host\bin\rom\`.

Optionally, run `.\build.bat test` to check the Halo sandbox and any packs you
imported. No game data is needed for the sandbox tests.

## 6. Play

```
.\build.bat run
```

Or start `build\host\bin\AnimalCrossing.exe` directly. Its working folder
must be `build\host\bin`.

At the title screen, press **Enter** with **Start Game** selected. With no
save, Animal Crossing begins a new game, as on the GameCube: the
train ride with Rover, then picking your name and town. Once you arrive,
press **F1** to switch to the Halo camera. About 40 Covenant are already
spread around the town; the ones far away sleep until they hear gunfire.

The game saves to `build\host\bin\save\card_a\DobutsunomoriP_MURA.gci`. To use
a town you already have in Dolphin, open **Tools > Memory Card Manager**,
select the Animal Crossing save, click **Export GCI**, and copy the exported
file to that path with that exact name. Keep a backup copy: the game
overwrites this file whenever you save.

### Controls

In Animal Crossing's camera: **WASD** to move, **Space** for A, **Left Shift**
for B, **Enter** for Start. These are set in
`build\host\bin\keybindings.ini`.

In the Halo camera (**F1**), the mouse is captured:

| Input | Action |
|---|---|
| Mouse | Look |
| WASD | Move |
| Space | Jump |
| Left Ctrl or C | Crouch |
| Left mouse | Fire (hold to charge the Plasma Pistol) |
| Mouse wheel, 1–9, 0 | Change weapon |
| Q or Tab | Last weapon |
| Right mouse or F | Throw a grenade |
| G | Switch between frag and plasma grenades |
| Z or middle mouse | Zoom |
| R | Reload |
| Esc | Release the mouse and open the pause menu |
| F8 | Invincibility on or off (on by default) |
| F10 | Hide or show the debug text |

Talking to someone hands control back to Animal Crossing until the
conversation ends. Walking into a door plays Animal Crossing's door animation.
You carry all ten Halo CE weapons. The debug keys are listed in
[README_DEV.md](README_DEV.md).

## The Tom Nook debt scene

You owe Tom Nook 4,980,000 Bells. You start in first person and are walked
into his shop. He greets you with a page of yelling, then leaves the counter
and follows you around: red in the face, huffing steam, and shouting the new
total every few seconds as interest piles up. Shoot him and he goes down,
and stays down until you leave. Come back in and he's up again, with funeral
costs added to your bill.

### Quickest way: straight into the shop, no save needed

This runs the scene inside the title screen's demo, so there are no menus and
nothing is saved. In PowerShell:

```
cd build\host\bin
$env:HC_SCENARIO='debt'; $env:HC_WARP='shop'; $env:HC_AUTO_TALK='1'; $env:HC_INVASION='0'
.\AnimalCrossing.exe
```

In Command Prompt:

```
cd build\host\bin
set HC_SCENARIO=debt
set HC_WARP=shop
set HC_AUTO_TALK=1
set HC_INVASION=0
AnimalCrossing.exe
```

Don't press anything at the title screen. About 10 seconds after launch the
demo starts in town and walks you through Nook's door. This works without
any save file. `HC_AUTO_TALK` presses A through
the dialogue for you. `HC_INVASION=0` keeps the Covenant out, so it's just
you and Nook. Leave it out if you want company. If the demo goes back to the
title screen, close the game and start it again to replay the scene.

### In your own town

Set only `HC_SCENARIO=debt`, then start the game and load your town as
normal:

```
cd build\host\bin
$env:HC_SCENARIO='debt'
.\AnimalCrossing.exe
```

Three seconds after you step outside, you're walked into Nook's shop. Here
the debt is your house loan, so you can pay it at the post office, and paying
it off calms him down. **If you save, the debt stays in your save file**, so
back up `DobutsunomoriP_MURA.gci` first. `HC_DEBT=<bells>`, up to 9999999,
sets a different starting amount.

Environment variables set with `$env:` or `set` only last for that window.
Open a new window to play normally again.

## Troubleshooting

| Problem | Fix |
|---|---|
| `MSYS2 not found at C:\msys64` | Install MSYS2, or set `MSYS2_ROOT` to where it is. |
| `no Animal Crossing disc image in assets_local/animal_crossing/` | Step 3. The file must end in `.iso`, `.gcm` or `.ciso`. |
| `UNSUPPORTED` from `check_disc.py` | Only the USA release (`GAFE01`) works. |
| `pacman` can't find a package | Run `pacman -Syu` in MINGW32 and try again. |
| `No module named numpy` | `pip install numpy` for the Windows Python you run the importer with. |
| Grey boxes instead of Halo models, beeps instead of sounds | The Halo packs are missing or out of date. Run step 4 again. The debug text shows `vm halo` when the imported weapons loaded. |
| The window opens and closes straight away | Start it from `build\host\bin`, and check that `build\host\bin\rom\` contains your disc image. Rebuilding with `.\build.bat` puts it back. |
| Stuck with the mouse captured | Press **Esc**. |

## More

* [README_DEV.md](README_DEV.md): every environment switch, debug keys,
  tests, and how the host patch is maintained.
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how Halo's sandbox is attached
  to the Animal Crossing engine.
* [assets_local/README.md](assets_local/README.md): what goes in the ignored
  local-data folder.
