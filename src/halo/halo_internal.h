/* Shared helpers between the Halo sandbox translation units. Not for hosts. */
#ifndef HALO_INTERNAL_H
#define HALO_INTERNAL_H

#include "halo_sim.h"

#define HALO_DT HALO_SECONDS_PER_TICK

static inline unsigned int halo_rand_u32(HaloSim* s) {
    unsigned int x = s->rng ? s->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

/* [0, 1) */
static inline float halo_randf(HaloSim* s) { return (halo_rand_u32(s) >> 8) * (1.0f / 16777216.0f); }
static inline float halo_rand_range(HaloSim* s, float lo, float hi) { return lo + (hi - lo) * halo_randf(s); }

/* Uniform direction inside a cone of `half_angle` around unit vector `dir`. */
hv3 halo_random_cone(HaloSim* s, hv3 dir, float half_angle);

void halo_emit(HaloSim* s, HaloEventType type, hv3 pos, hv3 dir, int unit, int other, int def, float value);

static inline int halo_unit_alive(const HaloSim* s, int i) {
    return i >= 0 && i < HALO_MAX_UNITS && s->units[i].active && !s->units[i].dead;
}

static inline hv3 halo_unit_center(const HaloUnit* u) {
    return hv3_make(u->pos.x, u->pos.y, u->pos.z + halo_unit_height(u) * 0.55f);
}

/* halo_biped.c */
void halo_biped_update(HaloSim* s, int unit);

/* halo_weapon.c */
void halo_weapon_init(HaloWeaponState* w, HaloWeaponId id);
void halo_weapon_update(HaloSim* s, int unit);

/* halo_projectile.c */
int halo_projectile_spawn(HaloSim* s, HaloProjectileId def, int owner, hv3 pos, hv3 vel, int target);
void halo_projectiles_update(HaloSim* s);

/* Segment vs. living units. Returns the unit index or -1; fills t (0..1) and head flag. */
int halo_ray_units(HaloSim* s, hv3 a, hv3 b, int ignore, float* out_t, int* out_head);
int halo_line_of_sight(HaloSim* s, hv3 from, hv3 to);

/* halo_damage.c */
void halo_area_damage(HaloSim* s, hv3 center, int attacker, const HaloDamageEffectDef* e);
void halo_damage_update(HaloSim* s, int unit);

/* halo_ai.c */
void halo_ai_spawned(HaloSim* s, int unit, HaloActorTypeId type);
void halo_ai_update(HaloSim* s, int unit);
void halo_ai_notify_damaged(HaloSim* s, int unit, int attacker);
void halo_ai_notify_killed(HaloSim* s, int unit, int killer);
void halo_ai_notify_noise(HaloSim* s, hv3 pos, float loudness_range, int source);
void halo_ai_notify_grenade_stuck(HaloSim* s, int unit);
int halo_ai_pick_target(HaloSim* s, int unit);

#endif
