/* Weapon state machine after halocea weapon_update.c / weapon_trigger_fire.c /
 * weapon_trigger_can_fire_again.c / weapon_trigger_release_charge.c, plus
 * grenade throws (unit_throw_grenade_release.c). */
#include "halo_internal.h"

#define CHARGE_START_DELAY 0.18f  /* holding past this begins an overcharge */
#define COOLDOWN_EPSILON 0.001f   /* tick-sum rounding must not skip a shot */

void halo_weapon_init(HaloWeaponState* w, HaloWeaponId id) {
    const HaloWeaponDef* d = &g_halo_weapons[id];
    HaloWeaponState z = { 0 };
    *w = z;
    w->id = id;
    w->rounds_loaded = d->rounds_loaded_maximum;
    w->rounds_reserve = d->rounds_total_initial;
    w->battery = 1.0f;
}

static int weapon_uses_magazine(const HaloWeaponDef* d) {
    return d->rounds_loaded_maximum > 0;
}

/* Bullet magnetism: bend the aim toward the best enemy inside the autoaim cone. */
static hv3 apply_magnetism(HaloSim* s, int ui, hv3 eye, hv3 dir, const HaloWeaponDef* wd, int* out_target) {
    *out_target = -1;
    if (wd->autoaim_angle <= 0.0f) return dir;
    HaloUnit* u = &s->units[ui];
    float best = cosf(wd->autoaim_angle);
    int best_i = -1;
    hv3 best_dir = dir;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (i == ui || !halo_unit_alive(s, i) || s->units[i].team == u->team) continue;
        if (!u->is_player && s->units[i].team == HALO_TEAM_NEUTRAL) continue;
        hv3 c = halo_unit_center(&s->units[i]);
        hv3 to = hv3_sub(c, eye);
        float dist = hv3_len(to);
        if (dist < 0.01f || dist > wd->magnetism_range) continue;
        hv3 n = hv3_scale(to, 1.0f / dist);
        float dp = hv3_dot(n, dir);
        if (dp > best && halo_line_of_sight(s, eye, c)) {
            best = dp;
            best_i = i;
            best_dir = n;
        }
    }
    *out_target = best_i;
    if (best_i < 0 || !u->is_player) return dir;
    /* Partial pull keeps it feeling like the player's aim. */
    return hv3_norm(hv3_lerp(dir, best_dir, 0.5f));
}

static void fire_round(HaloSim* s, int ui, int charged) {
    HaloUnit* u = &s->units[ui];
    HaloWeaponState* w = &u->weapon;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    const HaloWeaponTriggerDef* t = &wd->trigger;
    HaloProjectileId pid = charged ? t->charged_projectile : t->projectile;
    const HaloProjectileDef* pd = &g_halo_projectiles[pid];

    hv3 eye = halo_unit_eye(u);
    hv3 dir = hv3_from_angles(u->yaw, u->pitch);
    int target = -1;
    dir = apply_magnetism(s, ui, eye, dir, wd, &target);
    if (!u->is_player && s->ai[ui].active) {
        if (halo_unit_alive(s, s->ai[ui].target)) target = s->ai[ui].target;
    }

    float err = t->projectile_error_angle_lower_bound +
                (t->projectile_error_angle_upper_bound - t->projectile_error_angle_lower_bound) * w->error;
    if (!u->is_player && s->ai[ui].active) err += g_halo_actors[s->ai[ui].type].aim_error_angle;

    int count = t->projectiles_per_shot > 0 ? t->projectiles_per_shot : 1;
    for (int n = 0; n < count; n++) {
        hv3 d = halo_random_cone(s, dir, charged ? 0.0f : err);
        hv3 vel = hv3_scale(d, pd->initial_velocity);
        int pi = halo_projectile_spawn(s, pid, ui, eye, vel, pd->guided_angular_velocity > 0.0f ? target : -1);
        if (pi >= 0) {
            /* Render-only: start the tracer at the gun, not the eye. */
            float cy = cosf(u->yaw), sy = sinf(u->yaw);
            float f = wd->fp_offset[0], l = wd->fp_offset[1], up = wd->fp_offset[2];
            s->projectiles[pi].visual_offset = hv3_make(cy * f - sy * l, sy * f + cy * l, up);
        }
    }

    if (!s->infinite_ammo || !u->is_player) {
        if (weapon_uses_magazine(wd)) {
            w->rounds_loaded -= t->rounds_per_shot > 0 ? t->rounds_per_shot : 1;
            if (w->rounds_loaded < 0) w->rounds_loaded = 0;
        } else if (u->is_player) {
            /* AI batteries never run flat; Covenant don't scavenge. */
            w->battery = hc_maxf(0.0f, w->battery - wd->battery_per_round * (charged ? 5.0f : 1.0f));
        }
    }
    if (wd->heat_overheated_threshold > 0.0f) {
        w->heat += charged ? t->charged_heat : t->heat_generated_per_round;
        if (w->heat >= wd->heat_overheated_threshold) {
            w->heat = wd->heat_overheated_threshold;
            w->overheated = 1;
            halo_emit(s, HALO_EV_OVERHEAT, eye, dir, ui, -1, w->id, 0.0f);
        }
    }
    w->fire_flash = 1.0f;
    w->recoil = 1.0f;
    halo_emit(s, HALO_EV_WEAPON_FIRED, eye, dir, ui, -1, w->id, charged ? 1.0f : 0.0f);
    halo_ai_notify_noise(s, eye, 14.0f, ui);
}

static void start_reload(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    HaloWeaponState* w = &u->weapon;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    if (!weapon_uses_magazine(wd) || w->reload_timer > 0.0f) return;
    if (w->rounds_loaded >= wd->rounds_loaded_maximum) return;
    if (u->is_player && !s->infinite_ammo && w->rounds_reserve <= 0) return;
    w->reload_timer = wd->reload_time;
    w->charge = 0.0f;
    halo_emit(s, HALO_EV_RELOAD, halo_unit_eye(u), hv3_make(0, 0, 0), ui, -1, w->id, 0.0f);
}

/* Returns 1 if another shell should follow (per-round reloads). */
static int finish_reload(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    HaloWeaponState* w = &u->weapon;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    int need = wd->rounds_loaded_maximum - w->rounds_loaded;
    if (wd->reload_per_round && need > 1) need = 1;
    if ((s->infinite_ammo && u->is_player) || !u->is_player) {
        w->rounds_loaded += need;
    } else {
        int take = need < w->rounds_reserve ? need : w->rounds_reserve;
        w->rounds_loaded += take;
        w->rounds_reserve -= take;
    }
    int more = w->rounds_loaded < wd->rounds_loaded_maximum &&
               (w->rounds_reserve > 0 || !u->is_player || s->infinite_ammo);
    return wd->reload_per_round && more;
}

/* Player arsenal: wheel / number keys / swap-to-last. Returns 1 if it switched. */
static int update_arsenal(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    if (!s->arsenal_enabled || !u->is_player || u->grenade_timer > 0.0f) return 0;
    HaloWeaponState* w = &u->weapon;
    HaloUnitControl* c = &u->control;
    int cur = w->id;
    int want = -1;
    if (c->weapon_select > 0 && c->weapon_select <= HALO_WEAPON_COUNT) {
        want = c->weapon_select - 1;
    } else if (c->weapon_cycle != 0 && cur >= 0) {
        int step = c->weapon_cycle > 0 ? 1 : -1;
        for (int k = 1; k < HALO_WEAPON_COUNT; k++) {
            int cand = ((cur + step * k) % HALO_WEAPON_COUNT + HALO_WEAPON_COUNT) % HALO_WEAPON_COUNT;
            if (s->arsenal_owned[cand]) {
                want = cand;
                break;
            }
        }
    } else if (c->swap_pressed) {
        want = s->arsenal_last;
    }
    if (want < 0 || want >= HALO_WEAPON_COUNT || want == cur || !s->arsenal_owned[want]) return 0;
    if (cur >= 0) {
        HaloWeaponState* slot = &s->arsenal[cur];
        *slot = *w;
        slot->reload_timer = 0.0f;
        slot->charge = 0.0f;
        slot->fire_flash = slot->recoil = 0.0f;
        slot->trigger_was_down = 0;
        s->arsenal_last = (HaloWeaponId)cur;
    }
    int down = w->trigger_was_down;
    *w = s->arsenal[want];
    w->ready_timer = g_halo_weapons[want].ready_time;
    w->trigger_was_down = down; /* no free shot from a held trigger */
    halo_emit(s, HALO_EV_WEAPON_SWITCHED, halo_unit_eye(u), hv3_make(0, 0, 0), ui, -1, want, 0.0f);
    return 1;
}

static void update_grenade(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    /* The type is fixed from button press to release. */
    if (u->control.grenade_cycle_pressed && u->grenade_timer <= 0.0f) {
        int other = (u->grenade_type + 1) % HALO_GRENADE_COUNT;
        if (u->grenades[other] > 0 || u->grenades[u->grenade_type] <= 0) u->grenade_type = (HaloGrenadeId)other;
    }
    if (u->grenade_timer <= 0.0f && u->grenades[u->grenade_type] <= 0) {
        for (int g = 0; g < HALO_GRENADE_COUNT; g++) {
            if (u->grenades[g] > 0) {
                u->grenade_type = (HaloGrenadeId)g;
                break;
            }
        }
    }
    HaloGrenadeId type = u->grenade_type;
    const HaloGrenadeDef* gd = &g_halo_grenades[type];
    if (u->control.grenade_pressed && u->grenade_timer <= 0.0f && u->grenades[type] > 0) {
        u->grenade_timer = gd->throw_delay;
        u->weapon.reload_timer = 0.0f; /* throwing cancels a reload */
        u->weapon.charge = 0.0f;
        if (!(s->infinite_ammo && u->is_player)) u->grenades[type]--;
    }
    if (u->grenade_timer > 0.0f) {
        u->grenade_timer -= HALO_DT;
        if (u->grenade_timer <= 0.0f) {
            u->grenade_timer = 0.0f;
            const HaloProjectileDef* pd = &g_halo_projectiles[gd->projectile];
            hv3 dir = hv3_from_angles(u->yaw, hc_minf(u->pitch + gd->throw_pitch_bias, HC_DEG2RAD(80.0f)));
            hv3 eye = halo_unit_eye(u);
            hv3 pos = hv3_mad(eye, dir, 0.12f);
            hv3 vel = hv3_add(hv3_scale(dir, gd->throw_velocity), hv3_scale(u->vel, 0.5f));
            int pi = halo_projectile_spawn(s, gd->projectile, ui, pos, vel, -1);
            if (pi >= 0) s->projectiles[pi].timer = pd->detonation_timer;
            halo_emit(s, HALO_EV_GRENADE_THROWN, pos, dir, ui, -1, gd->projectile, 0.0f);
        }
    }
}

void halo_weapon_update(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    update_grenade(s, ui);

    HaloWeaponState* w = &u->weapon;
    if (w->id == HALO_WEAPON_NONE) return;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    const HaloWeaponTriggerDef* t = &wd->trigger;
    int down = u->control.fire && u->grenade_timer <= 0.0f;
    int pressed = down && !w->trigger_was_down;
    int released = !down && w->trigger_was_down;
    int fired = 0;

    w->fire_flash = hc_maxf(0.0f, w->fire_flash - HALO_DT * 12.0f);
    w->recoil = hc_maxf(0.0f, w->recoil - HALO_DT * 9.0f);
    if (w->fire_cooldown > 0.0f) w->fire_cooldown -= HALO_DT;

    if (wd->heat_overheated_threshold > 0.0f) {
        w->heat = hc_maxf(0.0f, w->heat - wd->heat_loss_per_second * HALO_DT);
        if (w->overheated && w->heat <= wd->heat_recovery_threshold) w->overheated = 0;
    }

    if (update_arsenal(s, ui)) return;
    if (!s->arsenal_enabled && u->control.swap_pressed && u->holstered.id != HALO_WEAPON_NONE &&
        u->grenade_timer <= 0.0f) {
        HaloWeaponState tmp = u->holstered;
        u->holstered = *w;
        u->holstered.reload_timer = 0.0f;
        u->holstered.charge = 0.0f;
        *w = tmp;
        w->ready_timer = g_halo_weapons[w->id].ready_time;
        w->trigger_was_down = down;
        return;
    }

    if (w->ready_timer > 0.0f) {
        w->ready_timer -= HALO_DT;
        w->trigger_was_down = down;
        return;
    }

    if (w->reload_timer > 0.0f && wd->reload_per_round && pressed && w->rounds_loaded > 0) {
        w->reload_timer = 0.0f; /* pump-action: pull the trigger to stop loading shells */
    }
    if (w->reload_timer > 0.0f) {
        w->reload_timer -= HALO_DT;
        if (w->reload_timer <= 0.0f) {
            w->reload_timer = finish_reload(s, ui) ? wd->reload_time : 0.0f;
        }
        w->trigger_was_down = down;
        return;
    }

    if (u->control.reload_pressed) {
        start_reload(s, ui);
        if (w->reload_timer > 0.0f) {
            w->trigger_was_down = down;
            return;
        }
    }

    int has_ammo = weapon_uses_magazine(wd) ? (w->rounds_loaded > 0) : (w->battery > 0.0f);
    int can_fire = has_ammo && !w->overheated && !u->dead;

    if (t->charging_time > 0.0f && u->is_player) {
        if (pressed && can_fire && w->fire_cooldown <= COOLDOWN_EPSILON) {
            fire_round(s, ui, 0);
            w->fire_cooldown = 1.0f / t->initial_rate_of_fire;
            w->charge = 0.0f;
            fired = 1;
        } else if (down && can_fire) {
            w->charge += HALO_DT;
        }
        if (released) {
            if (can_fire && w->charge >= t->charging_time + CHARGE_START_DELAY) {
                fire_round(s, ui, 1);
                w->fire_cooldown = 1.0f / t->initial_rate_of_fire;
                fired = 1;
            }
            w->charge = 0.0f;
        }
        if (!can_fire) w->charge = 0.0f;
    } else if (t->automatic) {
        w->trigger_time = down ? w->trigger_time + HALO_DT : 0.0f;
        if (down && can_fire && w->fire_cooldown <= COOLDOWN_EPSILON) {
            float frac = t->rate_of_fire_acceleration_time > 0.0f
                             ? hc_clampf(w->trigger_time / t->rate_of_fire_acceleration_time, 0.0f, 1.0f)
                             : 1.0f;
            float rof = t->initial_rate_of_fire + (t->final_rate_of_fire - t->initial_rate_of_fire) * frac;
            fire_round(s, ui, 0);
            float period = 1.0f / hc_maxf(rof, 0.1f);
            w->fire_cooldown += period;
            if (w->fire_cooldown < 0.0f) w->fire_cooldown = 0.0f;
            fired = 1;
        }
    } else {
        /* Semi-automatic. AI "clicks" by toggling control.fire. */
        if (pressed && can_fire && w->fire_cooldown <= COOLDOWN_EPSILON) {
            fire_round(s, ui, 0);
            w->fire_cooldown = 1.0f / hc_maxf(t->initial_rate_of_fire, 0.1f);
            fired = 1;
        }
    }
    if (w->fire_cooldown < 0.0f && !down) w->fire_cooldown = 0.0f;

    if (fired) {
        if (t->error_acceleration_time > 0.0f) {
            float shots_to_max = t->error_acceleration_time * hc_maxf(t->final_rate_of_fire, 1.0f);
            w->error = hc_minf(1.0f, w->error + 1.0f / hc_maxf(shots_to_max, 1.0f));
        }
    } else if (!down || !can_fire) {
        if (t->error_deceleration_time > 0.0f) w->error = hc_maxf(0.0f, w->error - HALO_DT / t->error_deceleration_time);
        else w->error = 0.0f;
    }

    /* Halo reloads on its own once the magazine runs dry. */
    if (weapon_uses_magazine(wd) && w->rounds_loaded <= 0) start_reload(s, ui);

    w->trigger_was_down = down;
}
