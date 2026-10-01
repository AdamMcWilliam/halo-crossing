/* The one Halo <-> Animal Crossing conversion layer. Nothing else in the tree
 * may hard-code a cross-world factor; see docs/WORLD_SCALE.md.
 *
 * Halo:            world units (1 wu = 3.048 m), Z up, yaw from +X toward +Y.
 * Animal Crossing: engine units (1 tile = 40, 1 acre = 640), Y up, X east,
 *                  Z south; actor yaw s16 with forward = (sin a, 0, cos a).
 *
 * Mapping: ac = (h.x, h.z, -h.y) * HALO_TO_AC_SCALE (a proper rotation, so
 * handedness is preserved; Halo +Y is AC north). ac_yaw = halo_yaw + pi/2.
 *
 * The scale is anchored on character height: the Chief's standing eye
 * (0.62 wu) lands at the AC player's eye height. Everything else (projectile
 * speed, gravity, collision radii, weapon offsets, AI perception) follows
 * from the same factor because the sandbox works purely in wu. */
#ifndef HC_WORLD_SCALE_H
#define HC_WORLD_SCALE_H

#include "halo/hc_vec.h"
#include "types.h"
#include "m_lib.h"

/* AC units per Halo world unit. Measured: see docs/WORLD_SCALE.md. */
#define HALO_TO_AC_SCALE 48.0f
#define AC_TO_HALO_SCALE (1.0f / HALO_TO_AC_SCALE)

/* Radians <-> AC binary angle (65536 per turn). */
#define HC_RAD_TO_S16(r) ((s16)(int)((r) * (32768.0f / HC_PI)))
#define HC_S16_TO_RAD(a) ((float)(s16)(a) * (HC_PI / 32768.0f))

static inline xyz_t hc_h2a_pos(hv3 h) {
    xyz_t a;
    a.x = h.x * HALO_TO_AC_SCALE;
    a.y = h.z * HALO_TO_AC_SCALE;
    a.z = -h.y * HALO_TO_AC_SCALE;
    return a;
}

static inline hv3 hc_a2h_pos(xyz_t a) {
    return hv3_make(a.x * AC_TO_HALO_SCALE, -a.z * AC_TO_HALO_SCALE, a.y * AC_TO_HALO_SCALE);
}

/* Directions: same axes, no scale. */
static inline xyz_t hc_h2a_dir(hv3 h) {
    xyz_t a;
    a.x = h.x;
    a.y = h.z;
    a.z = -h.y;
    return a;
}

static inline float hc_h2a_len(float wu) { return wu * HALO_TO_AC_SCALE; }
static inline float hc_a2h_len(float ac) { return ac * AC_TO_HALO_SCALE; }

static inline s16 hc_h2a_yaw(float halo_yaw) { return HC_RAD_TO_S16(halo_yaw + HC_PI * 0.5f); }
static inline float hc_a2h_yaw(s16 ac_yaw) { return hc_wrap_angle(HC_S16_TO_RAD(ac_yaw) - HC_PI * 0.5f); }

#endif
