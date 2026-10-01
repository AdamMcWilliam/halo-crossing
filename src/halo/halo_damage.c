/* Damage after halocea object_cause_damage.c / object_damage_shield.c /
 * object_damage_body.c / object_damage_update.c. Shields soak first using the
 * per-category shield multiplier; whatever the shield could not hold spills
 * to the body. Any hit restarts the shield stun timer; once it lapses the
 * shield recharges linearly. Body vitality never regenerates (CE). */
#include "halo_internal.h"

void halo_damage_unit(HaloSim* s, int vi, int attacker, const HaloDamageEffectDef* e, float scale,
                      hv3 toward_attacker, int head) {
    if (!halo_unit_alive(s, vi)) return;
    HaloUnit* v = &s->units[vi];
    const HaloBipedDef* d = halo_unit_def(v);
    float dmg = halo_rand_range(s, e->damage_lower_bound, e->damage_upper_bound) * scale;
    if (dmg <= 0.0f) return;

    v->last_damage_dir = toward_attacker;
    v->last_damage_age = 0.0f;
    v->last_attacker = attacker;

    if (d->maximum_shield_vitality > 0.0f) {
        v->shield_stun = d->shield_stun_time;
        v->shield_recharging = 0;
    }

    if (v->is_player && s->infinite_shields) {
        v->shield_flash = 1.0f;
        halo_emit(s, HALO_EV_UNIT_DAMAGED, halo_unit_center(v), toward_attacker, vi, attacker, e->category, 0.0f);
        return;
    }

    float to_body = dmg;
    float shield_mult = d->shield_damage_multiplier[e->category];
    if (v->shield > 0.0f && shield_mult > 0.0f) {
        float shield_dmg = dmg * shield_mult;
        if (shield_dmg <= v->shield) {
            v->shield -= shield_dmg;
            to_body = 0.0f;
        } else {
            to_body = dmg * (shield_dmg - v->shield) / shield_dmg;
            v->shield = 0.0f;
            halo_emit(s, HALO_EV_SHIELD_DEPLETED, halo_unit_center(v), toward_attacker, vi, attacker, 0, 0.0f);
        }
        v->shield_flash = 1.0f;
    }

    float body_dmg = 0.0f;
    if (to_body > 0.0f) {
        body_dmg = to_body * d->body_damage_multiplier[e->category];
        if (head && e->precision) body_dmg *= d->head_damage_multiplier;
        v->body -= body_dmg;
        v->hurt_flash = 1.0f;
    }

    if (e->instantaneous_acceleration > 0.0f) {
        /* biped_accelerate.c: living bipeds take half the impulse. */
        float k = e->instantaneous_acceleration * 0.5f;
        v->vel = hv3_mad(v->vel, toward_attacker, -k);
    }

    halo_emit(s, HALO_EV_UNIT_DAMAGED, halo_unit_center(v), toward_attacker, vi, attacker, e->category, dmg);

    if (v->body <= 0.0f) {
        halo_kill_unit(s, vi, attacker);
    } else {
        halo_ai_notify_damaged(s, vi, attacker);
    }
}

void halo_area_damage(HaloSim* s, hv3 center, int attacker, const HaloDamageEffectDef* e) {
    float inner = e->radius_inner, outer = e->radius_outer;
    if (outer <= 0.0f) return;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (!halo_unit_alive(s, i)) continue;
        HaloUnit* u = &s->units[i];
        hv3 c = halo_unit_center(u);
        float dist = hv3_dist(c, center);
        float r = halo_unit_def(u)->collision_radius;
        float dd = hc_maxf(0.0f, dist - r);
        if (dd >= outer) continue;
        float k = dd <= inner ? 1.0f : 1.0f - (dd - inner) / (outer - inner);
        if (k <= 0.0f) continue;
        hv3 lift = hv3_make(center.x, center.y, center.z + 0.05f);
        if (dist > 0.3f && !halo_line_of_sight(s, lift, c)) continue;
        hv3 toward = dist > 1e-4f ? hv3_scale(hv3_sub(center, c), 1.0f / dist) : hv3_make(0, 0, -1);
        /* Blasts throw upward a little more than they push sideways. */
        toward.z -= 0.6f;
        toward = hv3_norm(toward);
        halo_damage_unit(s, i, attacker, e, k, toward, 0);
        s->units[i].grounded = 0;
    }
}

void halo_damage_update(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    const HaloBipedDef* d = halo_unit_def(u);
    u->hurt_flash = hc_maxf(0.0f, u->hurt_flash - HALO_DT * 3.0f);
    u->shield_flash = hc_maxf(0.0f, u->shield_flash - HALO_DT * 4.0f);
    if (d->maximum_shield_vitality <= 0.0f || u->dead) return;

    if (u->is_player && s->infinite_shields) {
        u->shield = d->maximum_shield_vitality;
        u->shield_stun = 0.0f;
        return;
    }
    if (u->shield_stun > 0.0f) {
        u->shield_stun -= HALO_DT;
        return;
    }
    if (u->shield < d->maximum_shield_vitality) {
        if (!u->shield_recharging) {
            u->shield_recharging = 1;
            halo_emit(s, HALO_EV_SHIELD_RECHARGE, halo_unit_center(u), hv3_make(0, 0, 0), ui, -1, 0, 0.0f);
        }
        float rate = d->maximum_shield_vitality / hc_maxf(d->shield_recharge_time, 0.1f);
        u->shield = hc_minf(d->maximum_shield_vitality, u->shield + rate * HALO_DT);
        if (u->shield >= d->maximum_shield_vitality) u->shield_recharging = 0;
    }
}
