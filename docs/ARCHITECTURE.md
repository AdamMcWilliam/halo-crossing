# Halo Crossing architecture

Halo: Combat Evolved's combat sandbox running inside an Animal Crossing (GameCube)
town. Animal Crossing provides the world: terrain, buildings, villagers, rendering.
Halo provides the rules: Master Chief's movement and shields, weapons, projectiles,
grenades, Covenant AI, damage and HUD.

No retail data lives in this repository. The host reads the user's own disc image
from `assets_local/`, and every Halo value is a documented number in
`src/halo/halo_tuning.c`. Halo's look and sound come from the user's own Xbox
image: `tools/import_halo.py` converts models, animations, bitmaps and sounds
into packs under `assets_local/halo/generated/`, and the game falls back to
placeholders for anything missing. Gameplay numbers are not imported yet.

## 1. Inputs

| | ACGC-PC-Port (host) | halocea | halo-punpckhdq | halo-re |
|---|---|---|---|---|
| What it is | Full decomp of Animal Crossing GAFE01 plus a native PC layer | Non-matching reconstruction of the Blam! (CE) engine from the HCEA Xbox 360 PDB | Matching decomp of the Xbox CE beta, early WIP | Patching framework for the Xbox XBE |
| Language | C (game), C++ (emu64 renderer, PC layer) | C, some C++ | C plus Python tooling | C plus Python |
| Build | CMake + Ninja, MinGW **i686** (32-bit), SDL2, OpenGL 3.3 via GLAD | No runnable build (research corpus) | `configure.py` + Ninja + Xbox SDK | CMake for MSVC/Clang, outputs an Xbox XBE |
| Runs standalone on PC | **Yes** | No | No | No (xemu/Xbox) |
| Data needed | User's disc image | `.map` files (none here) | Retail XBE | Retail XBE |
| License | Upstream repo license | Research notice only | CC0 | None |
| Useful for | Hosting everything | Algorithm structure and names | Tag struct layouts | Nothing combat-related yet |

Animal Crossing's game code builds N64-style F3DEX2 display lists. `emu64`
translates them to GX, and the PC layer turns GX into OpenGL. The game runs
at 60 fps with a variable `graph->dt`. Halo runs a fixed 30 Hz tick.

## 2. Host choice

**Option A: Animal Crossing hosts, and Halo's gameplay is reimplemented as a
portable C sandbox.** This is the one we chose.

* The AC port is the only input that boots on a PC today. None of the Halo
  sources link into a playable program: halocea has no runtime, data or
  renderer, and the other two are Xbox-only and incomplete.
* The Halo sandbox needs only a small world interface (move a capsule, cast a
  ray, find ground, path, cover). AC's collision (`mCoBG_*`) can answer all of
  that through one adapter.
* AC's world needs a renderer, scene streaming, actors, scripting, a message
  system and the disc filesystem. Moving that into another engine would be the
  bulk of the work.

**Option B: Halo hosts and imports the AC town.** This is blocked: there is no
runnable Halo PC engine source, so we would have to finish a Blam! port first.
Converting AC's acre/BG/actor model into BSP and scenario tags is a second
project on top of that.

**Option C: a neutral new engine imports both games' assets.** This means
rewriting AC's world behaviour from scratch (villager AI, events, acres,
buildings, time of day) as well as Halo's. It costs the most and shows a Grunt
standing next to Tom Nook last.

## 3. What is reused, adapted and reimplemented

| Item | Treatment |
|---|---|
| Halo constants (30 Hz tick, 3.048 m world unit, gravity 0.00356517918 wu/tick², cyborg base speeds, 0.5× living-biped knockback) | **Reused** as documented values in `halo_units.h` / `halo_tuning.c` |
| Tag layouts (weapon trigger, projectile, damage effect, biped, actor) | **Reused as a shape**: `halo_defs.h` mirrors the fields we use, so a tag importer can fill the same structs later |
| Biped movement, weapon trigger state machine, projectile update, shield/body damage, panic and flee | **Reimplemented** in `src/halo/` from halocea's algorithms; no code copied |
| Collision, ground and line of sight | **Adapter** `hc_ac_world.c` over `mCoBG_VirtualBGCheck`, `mCoBG_LineCheck_RemoveFg`, `mCoBG_GetBgY_*` |
| Pathfinding and cover | **Adapter** with A* over AC's 40-unit tile grid, using collision for edges |
| Rendering | **Adapter** `hc_gfx.c`: AC display lists in private arenas. Imported Halo models are skinned on the CPU and lit by AC's sun and ambient light (`hc_fp_view.c`, `hc_biped_view.c`); procedural boxes (`hc_models.c`) stand in for anything not imported |
| HUD | **Imported** Halo HUD bitmaps and layout (`hud_pack.py`), drawn as texture rectangles by `hc_hud_view.c`; the text HUD in `hc_draw.c` covers what the pack lacks |
| Input | **Adapter** `hc_input.c`: SDL mouse/keyboard to `HaloUnitControl`; the AC pad is muted while the Halo camera is live |
| Camera | **Adapter**: overrides `play->view` after AC's camera runs |
| Audio | **Imported** Halo sounds (`sound_pack.py`), mixed in 3D on a separate SDL audio device by `hc_audio.c` / `hc_sound.c` |
| Villager reactions | **Adapter** `hc_villagers.c`: neutral hit volumes that follow AC's NPC actors; panic, hiding, knock-downs, Tom Nook's death |
| Halo AI behaviour trees, encounters, firing positions, animation graphs, vehicles | **Simplified**: a state machine (idle, alert, combat, search, flee, dead) |

## 4. Layout

```
halo-crossing/
  external/ACGC-PC-Port/   host, git submodule pinned at upstream 4099d24
  patches/host/            the only host changes: hook call sites (+45 lines)
  cmake/halo_crossing.cmake  adds src/** to the host target when HC_ROOT is set
  src/halo/                engine-free Halo sandbox (pure C, unit-tested headless)
    halo_units.h           ticks, world units, gravity
    halo_defs.h            tag-shaped definitions
    halo_tuning.c          THE data table; every value marked [engine] or [approx]
    halo_sim.c/.h          units, fixed tick, events, spawn/respawn
    halo_biped.c           movement, jump, crouch, ground snap
    halo_weapon.c          rate of fire, error, magazines, heat, charge, grenades
    halo_projectile.c      flight, guidance, stick, detonate
    halo_damage.c          shield/body split, stun, recharge, area damage
    halo_ai.c              Grunt/Elite states, squads, panic
    halo_world.h           the world interface the sandbox needs (INavigationWorld)
  src/integration/         Halo <-> Animal Crossing adapters
    hc_world_scale.h       the single Halo<->AC scale and axis conversion
    hc_ac_world.c          collision, raycast, ground, can_walk, A*, cover
    hc_gfx.c               display-list arenas and primitives
    hc_models.c            placeholder Covenant/Chief/viewmodel geometry
    hc_draw.c              world pass, sky, placeholder HUD
    hc_fp_pack.c           loader, posing and skinning for the model packs
    hc_fp_view.c           first-person arms and weapon
    hc_biped_view.c        Grunts, Elites and their weapons
    hc_hud_pack.c, hc_hud_view.c  Halo's HUD
    hc_sound.c, hc_audio.c sound pack, 3D mixer, sim events to sounds
    hc_villagers.c         villagers and Tom Nook in the firefight
    hc_input.c             SDL input to Halo controls
    hc_hooks.h             hook signatures called from the host
  src/prototype/           Milestone glue: hooks, camera, debug keys, overlay
  tests/                   headless tests (gcc; pack checks need imported data)
  tools/                   patching, disc check, references, devctl smoke driver
    import_halo.py         reads the user's Halo Xbox image, writes the packs
    halo_import/           XDVDFS, cache map, model, animation, bitmap and sound readers
  assets_local/            user's own game data and generated packs (git-ignored)
```

## 5. Frame flow

Each host frame (`play_main`):

1. `Game_play_move`: actors update, then **`hc_hook_play_update`**. This
   handles debug keys, the camera mode, input, `halo_sim_advance(dt)` (0–4
   fixed 30 Hz ticks plus an interpolation alpha), syncing the Halo player and
   the AC player, events to effects and log, and the overlay.
2. `Camera2_process`, then **`hc_hook_camera`**. In Halo mode this replaces the
   eye, target and FOV in `play->view`, and queues the sky into BG_OPA.
3. `Game_play_draw`: AC actors draw, then **`hc_hook_draw_world`** (units,
   viewmodel, projectiles, effects).
4. After the frame is drawn, **`hc_hook_draw_hud`** (shields, health, ammo,
   grenades, reticle, motion sensor, damage arcs, zoom mask, text).

SDL events go through `hc_hook_sdl_event` before the port's own handling.
Pad 0 passes through `hc_hook_filter_pad`.

AC's per-frame display-list arenas are sized for AC alone: POLY_OPA has 9,952
Gfx words, POLY_XLU 2,048, FONT 1,792. `hc_gfx_begin()` points the host's
THA_GA arenas, including `GRAPH_ALLOC`, at private static buffers, and
`hc_gfx_end()` restores them and links ours with one `gSPDisplayList`.
emu64 treats addresses in the executable image as raw pointers, so static
buffers work.

## 6. Risks

| Risk | Status / mitigation |
|---|---|
| Scale mismatch between a 3.048 m Halo unit and AC's 40-unit tiles | One constant (`HALO_TO_AC_SCALE`, see WORLD_SCALE.md); calibrated against the villager's head height |
| AC collision is a 2.5D tile heightfield with wall attributes, not a BSP | Rays are stepped at ≤28 units because LineCheck only scans 3×3 tiles; ground comes from the heightfield; buildings come from wall attributes |
| Display-list budget | Private arenas (above); overflow counter on the overlay |
| AC's camera and scripts fight a free FPS camera | Override after `Camera2_process`; hand control back to AC whenever a message window is open |
| AC state assumptions (talking, demos, scene changes) | Hooks reset the sandbox per scene; the AC player is moved but never despawned |
| Authentic Halo numbers live in tags, not in the reference code | Documented values now, each tagged `[engine]`/`[approx]`; tag import later |
| Copyright | Nothing retail in git: user dumps in `assets_local/`, host data read from the user's disc, Halo packs generated locally from the user's image |
| 32-bit MinGW only | The sandbox is portable C; only the host needs i686 |

## 7. Milestones

| # | Milestone | Done when |
|---|---|---|
| 0 | Animal Crossing boots | Host builds from this repo and reaches the town |
| 1 | **Shoot a Grunt in Animal Crossing** | F1 Halo camera in town, AR with crosshair, one Grunt approaches and shoots, Chief's shields absorb it, Grunt dies |
| 2 | Sandbox complete for CE's opening loadout | Plasma Pistol charge/overheat, plasma grenade stick, Elite with shields and melee, respawn |
| 3 | Villagers react | NORMAL/ALERTED/PANICKING/HIDING with lines; AC sound cues for weapons and impacts |
| 4 | Town Plaza encounter | Waves: 4 Grunts; 4 Grunts + 1 Elite; 2 Elites + 6 Grunts; then a reward |
| 5 | Arsenal | Magnum, Plasma Rifle, Needler, Shotgun, Rocket Launcher |
| 6 | Look | Mode A (Halo look, imported models) and Mode B (AC-styled Covenant) |
| 7 | Data | Tag import from the user's Halo dump, replacing `halo_tuning.c` values |

## 8. Debug keys

See [README_DEV.md](../README_DEV.md#debug-keys) for the debug keys and the
overlay.
