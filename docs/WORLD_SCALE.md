# World scale and axes

Everything that converts between the two worlds goes through
`src/integration/hc_world_scale.h`. Other code never multiplies by a scale
constant itself.

| | Halo | Animal Crossing |
|---|---|---|
| Unit | world unit (wu) = 10 ft = 3.048 m | game unit; a ground tile is 40 units, an acre 640 |
| Up axis | +Z | +Y |
| Forward at yaw 0 | +X | +Z (`s16` angle, 0x10000 = 360°) |
| Handedness | right | right |
| Tick | fixed 30 Hz | variable frame `graph->dt` (60 fps nominal) |

Position: `ac = (h.x, h.z, -h.y) * HALO_TO_AC_SCALE` and back.
Yaw: `ac_yaw = halo_yaw + π/2`, in `s16` units.

## HALO_TO_AC_SCALE = 48

At 48 AC units per wu, a 40-unit tile is about 2.5 m.

| Thing | Halo | AC units |
|---|---|---|
| Master Chief standing height (0.70 wu) | 2.13 m | 34 |
| Chief eye (0.62 wu) | 1.89 m | 30 |
| Grunt height (0.46 wu) | 1.40 m | 22 |
| Villager head joint (measured, overlay `head`) | | 21–22 above feet |
| Chief run (2.25 wu/s) | 6.9 m/s | 108 units/s, about 2.7 tiles/s |
| AR bullet (200 wu/s) | 610 m/s | 9,600 units/s |
| Motion tracker radius (8.2 wu) | 25 m | 394, about 10 tiles |

The Chief comes out a little taller than an AC villager, and a Grunt stands
about villager height. Combat ranges of 2.5–7 wu (Grunt and Elite engagement
bands) become 3–8 tiles, which fits inside one acre. Retune with the overlay:
the `head` value is the AC player's head joint height above its feet, and the
`chief`/`ac` pair shows both coordinate systems for the same point.

The first-person weapon is drawn at `VM_SCALE` (0.25) of its real size and
proportionally closer to the eye. It looks the same but never clips into
walls, and Halo's own first-person weapon is likewise a separate projection.
