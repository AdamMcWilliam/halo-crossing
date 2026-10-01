/* Projectile integration after halocea projectile_update.c (guidance, gravity,
 * collide) and projectile_collision.c (attach/stick, detonation timers). */
#include "halo_internal.h"

#define OWNER_IMMUNE_TIME 0.15f

int halo_projectile_spawn(HaloSim* s, HaloProjectileId def, int owner, hv3 pos, hv3 vel, int target) {
    int slot = -1;
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++) {
        if (!s->projectiles[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;
    HaloProjectile* p = &s->projectiles[slot];
    HaloProjectile z = { 0 };
    *p = z;
    p->active = 1;
    p->def = def;
    p->owner = owner;
    p->team = (owner >= 0) ? s->units[owner].team : HALO_TEAM_COVENANT;
    p->pos = p->prev_pos = pos;
    p->vel = vel;
    p->timer = -1.0f;
    p->attached_unit = -1;
    p->target_unit = target;
    return slot;
}

/* Segment a->b against a vertical cylinder; returns entry t in [0,1] or -1. */
static float segment_vs_cylinder(hv3 a, hv3 b, hv3 base, float radius, float height) {
    hv3 d = hv3_sub(b, a);
    float fx = a.x - base.x, fy = a.y - base.y;
    float best = -1.0f;
    float A = d.x * d.x + d.y * d.y;
    float C = fx * fx + fy * fy - radius * radius;
    if (C <= 0.0f && a.z >= base.z && a.z <= base.z + height) return 0.0f;
    if (A > 1e-9f) {
        float B = 2.0f * (fx * d.x + fy * d.y);
        float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            float t = (-B - sqrtf(disc)) / (2.0f * A);
            if (t >= 0.0f && t <= 1.0f) {
                float z = a.z + d.z * t;
                if (z >= base.z && z <= base.z + height) best = t;
            }
        }
    }
    /* Caps. */
    if (fabsf(d.z) > 1e-9f) {
        float caps[2] = { base.z + height, base.z };
        for (int k = 0; k < 2; k++) {
            float t = (caps[k] - a.z) / d.z;
            if (t < 0.0f || t > 1.0f || (best >= 0.0f && t >= best)) continue;
            float px = fx + d.x * t, py = fy + d.y * t;
            if (px * px + py * py <= radius * radius) best = t;
        }
    }
    return best;
}

int halo_ray_units(HaloSim* s, hv3 a, hv3 b, int ignore, float* out_t, int* out_head) {
    int best = -1;
    float best_t = 2.0f;
    int head = 0;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (i == ignore || !halo_unit_alive(s, i)) continue;
        const HaloUnit* u = &s->units[i];
        const HaloBipedDef* d = halo_unit_def(u);
        float h = halo_unit_height(u);
        float t = segment_vs_cylinder(a, b, u->pos, d->collision_radius, h);
        if (t >= 0.0f && t < best_t) {
            best_t = t;
            best = i;
            float z = a.z + (b.z - a.z) * t;
            head = z >= u->pos.z + h * d->head_height_fraction;
        }
    }
    if (out_t) *out_t = best_t;
    if (out_head) *out_head = head;
    return best;
}

int halo_line_of_sight(HaloSim* s, hv3 from, hv3 to) {
    const HaloWorldApi* w = s->world;
    if (!w || !w->raycast) return 1;
    HaloRayHit hit;
    return !w->raycast(w->ctx, from, to, &hit);
}

static void detonate(HaloSim* s, int pi) {
    HaloProjectile* p = &s->projectiles[pi];
    const HaloProjectileDef* pd = &g_halo_projectiles[p->def];
    hv3 at = p->pos;
    if (p->attached_unit >= 0 && halo_unit_alive(s, p->attached_unit)) {
        /* A stuck grenade is lethal to its carrier regardless of falloff. */
        halo_damage_unit(s, p->attached_unit, p->owner, &pd->detonation_damage, 1.0f,
                         hv3_make(0, 0, 1), 0);
    }
    if (pd->detonation_damage.radius_outer > 0.0f) {
        halo_area_damage(s, at, p->owner, &pd->detonation_damage);
        halo_emit(s, HALO_EV_EXPLOSION, at, hv3_make(0, 0, 1), p->owner, -1, p->def,
                  pd->detonation_damage.radius_outer);
        halo_ai_notify_noise(s, at, 20.0f, p->owner);
    }
    p->active = 0;
}

static void stick(HaloSim* s, int pi, int unit, hv3 at) {
    HaloProjectile* p = &s->projectiles[pi];
    const HaloProjectileDef* pd = &g_halo_projectiles[p->def];
    p->vel = hv3_make(0, 0, 0);
    p->pos = at;
    if (unit >= 0) {
        p->attached_unit = unit;
        p->attached_serial = s->units[unit].serial;
        p->attach_offset = hv3_sub(at, s->units[unit].pos);
    } else {
        p->stuck = 1;
    }
    if (p->timer < 0.0f || p->timer > pd->detonation_timer_attached) p->timer = pd->detonation_timer_attached;
    halo_emit(s, HALO_EV_GRENADE_STUCK, at, hv3_make(0, 0, 1), p->owner, unit, p->def, 0.0f);
    if (unit >= 0) halo_ai_notify_grenade_stuck(s, unit);
}

static void steer(HaloSim* s, HaloProjectile* p, const HaloProjectileDef* pd) {
    if (!halo_unit_alive(s, p->target_unit)) {
        p->target_unit = -1;
        return;
    }
    hv3 to = hv3_sub(halo_unit_center(&s->units[p->target_unit]), p->pos);
    float speed = hv3_len(p->vel);
    if (speed < 1e-4f) return;
    hv3 cur = hv3_scale(p->vel, 1.0f / speed);
    hv3 want = hv3_norm(to);
    float c = hc_clampf(hv3_dot(cur, want), -1.0f, 1.0f);
    float ang = acosf(c);
    float max_turn = pd->guided_angular_velocity * HALO_DT;
    hv3 nd;
    if (ang <= max_turn || ang < 1e-4f) {
        nd = want;
    } else {
        nd = hv3_norm(hv3_lerp(cur, want, max_turn / ang));
    }
    p->vel = hv3_scale(nd, speed);
}

void halo_projectiles_update(HaloSim* s) {
    const HaloWorldApi* w = s->world;
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++) {
        HaloProjectile* p = &s->projectiles[i];
        if (!p->active) continue;
        const HaloProjectileDef* pd = &g_halo_projectiles[p->def];
        p->age += HALO_DT;
        p->visual_offset = hv3_scale(p->visual_offset, 0.55f);

        if (p->attached_unit >= 0) {
            HaloUnit* u = &s->units[p->attached_unit];
            if (!u->active || u->serial != p->attached_serial) {
                p->attached_unit = -1;
            } else {
                p->pos = hv3_add(u->pos, p->attach_offset);
            }
        }

        if (p->timer >= 0.0f) {
            p->timer -= HALO_DT;
            if (p->timer <= 0.0f) {
                detonate(s, i);
                continue;
            }
        }
        if (p->attached_unit >= 0 || p->stuck) continue;

        if (p->target_unit >= 0 && pd->guided_angular_velocity > 0.0f) steer(s, p, pd);
        p->vel.z -= pd->air_gravity_scale * HALO_GRAVITY * HALO_DT;

        hv3 next = hv3_mad(p->pos, p->vel, HALO_DT);
        int ignore = (p->age <= OWNER_IMMUNE_TIME + HALO_DT) ? p->owner : -1;
        float tu = 2.0f;
        int head = 0;
        int hit_unit = halo_ray_units(s, p->pos, next, ignore, &tu, &head);
        /* Friendly fire off for AI so Grunts don't mow each other down. */
        if (hit_unit >= 0 && s->units[hit_unit].team == p->team && !s->units[hit_unit].is_player &&
            p->owner >= 0 && !s->units[p->owner].is_player) {
            hit_unit = -1;
            tu = 2.0f;
        }

        HaloRayHit wh;
        float tw = 2.0f;
        if (w && w->raycast && w->raycast(w->ctx, p->pos, next, &wh)) tw = wh.fraction;

        if (hit_unit >= 0 && tu <= tw) {
            hv3 at = hv3_lerp(p->pos, next, tu);
            if (pd->attaches_to_units) {
                stick(s, i, hit_unit, at);
                continue;
            }
            hv3 back = hv3_norm(hv3_scale(p->vel, -1.0f));
            halo_damage_unit(s, hit_unit, p->owner, &pd->impact_damage, 1.0f, back, head);
            halo_emit(s, HALO_EV_PROJECTILE_IMPACT, at, back, p->owner, hit_unit, p->def, 0.0f);
            if (pd->detonation_damage.radius_outer > 0.0f) {
                p->pos = at;
                detonate(s, i);
            } else {
                p->active = 0;
            }
            continue;
        }
        if (tw <= 1.0f) {
            if (pd->attaches_to_world) {
                stick(s, i, -1, hv3_mad(wh.point, wh.normal, 0.02f));
                continue;
            }
            halo_emit(s, HALO_EV_PROJECTILE_IMPACT, wh.point, wh.normal, p->owner, -1, p->def, 0.0f);
            halo_ai_notify_noise(s, wh.point, 4.0f, p->owner);
            if (pd->detonation_damage.radius_outer > 0.0f) {
                p->pos = wh.point;
                detonate(s, i);
            } else {
                p->active = 0;
            }
            continue;
        }

        p->distance += hv3_dist(p->pos, next);
        p->pos = next;
        if (p->distance > pd->maximum_range || p->pos.z < -50.0f) p->active = 0;
    }
}
