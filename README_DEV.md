# Halo Crossing developer guide

An experimental fan prototype: Halo: Combat Evolved combat inside an Animal
Crossing (GameCube) town. Animal Crossing is the host engine (via the
[ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port) decomp). Halo's sandbox
is reimplemented in portable C from documented behaviour. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full picture.

**No game data is in this repository, and none may be committed.** You supply
your own legally obtained copies (see [assets_local/README.md](assets_local/README.md)).

## Prerequisites (Windows)

1. [MSYS2](https://www.msys2.org/). In the **MSYS2 MINGW32** shell, install:
   ```
   pacman -S git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja mingw-w64-i686-SDL2 mingw-w64-i686-gdb
   ```
2. Python 3 on PATH. Optional: Pillow, for `tools/devctl.py` screenshots.
3. Your Animal Crossing (USA, GAFE01) disc image at
   `assets_local/animal_crossing/<name>.iso` (or `.gcm`/`.ciso`).

## Build and run

```
git clone --recursive <this repo> halo-crossing
cd halo-crossing
build.bat           # Windows (runs build.sh in MINGW32)
build.bat run       # build and launch
build.bat test      # headless Halo sandbox tests, no game data needed
./build.sh ...      # same, from an MSYS2 MINGW32 shell
```

`build.sh` applies `patches/host/*.patch` to the submodule, configures the
port with `-DHC_ROOT=<repo>`, builds `build/host/bin/AnimalCrossing.exe`, and
hard-links your disc image into `build/host/bin/rom/`.

### Halo assets (optional)

With your own Halo: Combat Evolved Xbox image (XISO or a full redump), import
the Chief's first-person arms and weapons:

```
pip install numpy
python tools/import_halo.py --iso "D:/dumps/Halo - Combat Evolved (USA).iso"
```

This reads `maps/bloodgulch.map` from the image and writes
`assets_local/halo/generated/fp_weapons.hcpk` (about 9 MB, git-ignored): the
arms merged with each weapon, GameCube-format textures, and the idle, fire,
ready, reload, grenade, overheat and posing animations. The game loads it at
startup (`HC_HALO_ASSETS=<dir>` points elsewhere) and draws the real models
for every weapon it has. The Fuel Rod Gun has no first-person model on Xbox,
so it keeps the placeholder boxes, as does everything when no pack is
present. The debug overlay's gfx line ends in `vm halo` or `vm boxes`.

Environment switches:

| Variable | Effect |
|---|---|
| `HC_FIRST_PERSON=1` | Start in the Halo camera |
| `HC_AUTOSPAWN=1` | Spawn a Grunt the first time the Halo camera goes live |
| `HC_INVASION=0` | Don't populate the town with Covenant squads on entry |
| `HC_INVINCIBLE=0` | Start mortal (the Chief is invincible by default; F8 toggles) |
| `HC_NO_CAPTURE=1` | Never capture the mouse (for unattended runs) |
| `HC_EVENT_LOG=<path>` | Append combat events to a text file |
| `HC_ATTRACT=1` | Self-playing tour: cycles every weapon every 3 s, aims at the nearest visible target, keeps a Covenant in view, invincible. Only runs while the mouse isn't captured. |
| `HC_VILLAGER_ALL=1` | Treat scripted NPCs (Rover, Porter, shopkeepers) as shootable villagers too |

Saves live in `build/host/bin/save/card_a/DobutsunomoriP_MURA.gci`, the same
format as Dolphin's Memcard Manager "Export GCI". To skip the new-game intro,
drop an existing USA (`GAFE`) town save there. Keep an untouched copy in
`assets_local/saves/`, because the game overwrites the installed file when
you save.

The title-screen demo runs in your town, so `HC_FIRST_PERSON=1 HC_NO_CAPTURE=1
HC_ATTRACT=1 HC_VILLAGER_ALL=1` exercises the whole sandbox without touching a
save file.

## Controls

The Animal Crossing camera uses the port's bindings (`build/host/bin/keybindings.ini`):
WASD to move, Space for A, Left Shift for B, Enter for Start.

Press **F1** for the Halo camera. The mouse is captured, and AC's pad input is
muted while it is active.

| Input | Action |
|---|---|
| Mouse | Look |
| WASD | Move (Halo acceleration) |
| Space | Jump |
| Left Ctrl / C | Crouch |
| Left mouse | Fire (Plasma Pistol: hold to charge) |
| Mouse wheel | Next / previous weapon |
| 1–9, 0 | Select weapon 1–10 |
| Q / Tab | Swap to the last weapon |
| Right mouse / F | Throw grenade |
| G | Switch grenade type (frag / plasma) |
| Z / middle mouse | Zoom (Pistol 2×, Sniper 2× / 8×) |
| R | Reload |
| Esc | Release the mouse and open the port's pause menu |

Weapon order: Assault Rifle, Pistol, Shotgun, Sniper Rifle, Rocket Launcher,
Flamethrower, Plasma Pistol, Plasma Rifle, Needler, Fuel Rod Gun. You spawn
with all ten; ammo, battery and heat are kept per weapon while holstered.

Entering the town populates it with Covenant squads (about 40 units). Squads
far from you sleep until they hear gunfire or you come within 20 wu.

Villagers are shootable. They shout when hit, run from gunfire, explosions
and nearby Covenant, and get knocked down when their health runs out, then
get back up 20 seconds later. Scripted NPCs (Rover, Porter, shop staff) flinch
and complain but can't be knocked down unless `HC_VILLAGER_ALL=1`.

When a message window opens (talking to a villager), control and the camera
go back to Animal Crossing until it closes.

## Debug keys

| Key | |
|---|---|
| F1 | AC camera / Halo first-person |
| F2 | Collision volumes |
| F3 | AI state labels over enemies |
| F4 | AI nav paths |
| F5 / F6 | Spawn a Grunt / an Elite ahead of you |
| F7 | Full arsenal: refill every weapon and both grenade types |
| F8 | Invincible on/off (starts on; stays set across rooms) |
| F9 | Kill all Covenant |
| F10 | Toggle the debug overlay |
| F11 | Clear and repopulate the town's Covenant squads |

Overlay lines, top to bottom:
1. FPS, sim tick, camera mode, scene, AC player state index, villager head height
2. Halo and AC position
3. Weapon, ammo, shield, health, grenades
4. Covenant alive by AI state (and how many are asleep), kills, villager states
5. Display-list arena use and overflows, collision queries this frame, and
   whether the viewmodel is the imported Halo model or placeholder boxes
6. Latest combat events

## Testing

* `build.bat test` runs `tests/halo_sim_test.c` against a synthetic flat world.
  It covers movement, shields, AR rate of fire and spread, a Grunt fight, and
  plasma grenade stick. Set `HALO_TEST_VERBOSE=1` for traces. It also runs
  `tests/fp_pack_test.c`, which poses and skins every animation of every
  weapon in your imported pack (skipped if you haven't imported one).
* `tools/devctl.py` drives the running game for smoke tests: focus, held keys,
  mouse and screenshots. `tools/scripts/new_game_to_station.txt` plays a
  fresh save's intro until you stand on the town's train platform (about 5
  minutes, unattended):
  ```
  python tools/devctl.py run tools/scripts/new_game_to_station.txt --shot build/station.png
  python tools/devctl.py keys "f1:120 wait:800 f5:120 wait:1500 lmbdown wait:800 lmbup" --shot build/fight.png
  ```
* The build has no debug info, but exported symbols are enough for gdb:
  `gdb -p <pid> -batch -ex "break hc_hook_play_update" -ex continue -ex bt`.

## Changing the host

Host edits are limited to hook call sites wrapped in `#ifdef HALO_CROSSING`.
After editing files under `external/ACGC-PC-Port`, run
`python tools/refresh_host_patch.py` and commit the updated patch, not the
submodule's working tree. To update the port, bump the submodule, run
`python tools/apply_host_patches.py`, and fix any rejects.

## Reference material

`python tools/fetch_references.py` clones the Halo research decomps (halocea,
halo-punpckhdq, halo-re) into `external/ref/` (git-ignored). They are only read
for algorithms and structure; no code from them is built or copied.
`src/halo/halo_tuning.c` marks each value `[engine]` (confirmed in engine code)
or `[approx]` (documented CE behaviour, pending tag import).
