/* Covenant actor AI. A compact take on Halo's actor actions (headers/
 * actor_action.h: sleep/alert/fight/flee/search) with the panic rules from
 * actor_stimulus_prop_just_killed.c (leader killed -> chance to flee). */
#include "halo_internal.h"

#define AI_ARRIVE_DIST 0.3f
#define AI_PROXIMITY_SENSE 1.5f   /* noticed regardless of facing */
#define AI_LOSE_TARGET_TIME 4.0f
#define AI_SEARCH_TIME 8.0f
#define AI_SQUAD_RADIUS 8.0f
#define AI_REPATH_TIME 0.75f

static void set_state(HaloSim* s, int ui, HaloAiState st) {
    HaloAi* a = &s->ai[ui];
    if (a->state == st) return;
    a->state = st;
    a->state_time = 0.0f;
    a->has_move_goal = 0;
    a->path_count = 0;
    a->burst_timer = 0.0f;
    if (st == HALO_AI_ALERT) {
        halo_emit(s, HALO_EV_AI_ALERTED, halo_unit_center(&s->units[ui]), hv3_make(0, 0, 0), ui, a->target, a->type, 0);
    }
}

static void set_goal(HaloAi* a, hv3 goal, float speed) {
    if (!a->has_move_goal || hv3_dist_xy(goal, a->move_goal) > 0.5f) a->repath_timer = 0.0f;
    a->move_goal = goal;
    a->has_move_goal = 1;
    a->move_speed = speed;
}

static void start_flee(HaloSim* s, int ui, float panic) {
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];
    set_state(s, ui, HALO_AI_FLEE);
    float lo = ad->flee_time_min > 0.0f ? ad->flee_time_min : 2.0f;
    float hi = ad->flee_time_max > lo ? ad->flee_time_max : lo + 1.0f;
    a->flee_timer = halo_rand_range(s, lo, hi);
    a->panic_timer = panic;
    halo_emit(s, HALO_EV_AI_PANIC, halo_unit_center(&s->units[ui]), hv3_make(0, 0, 0), ui, a->target, a->type, panic);
}

void halo_ai_spawned(HaloSim* s, int ui, HaloActorTypeId type) {
    HaloAi* a = &s->ai[ui];
    HaloAi z = { 0 };
    *a = z;
    a->active = 1;
    a->type = type;
    a->state = HALO_AI_IDLE;
    a->target = -1;
    a->home = s->units[ui].pos;
    a->last_pos = s->units[ui].pos;
    a->idle_timer = halo_rand_range(s, 0.5f, 2.5f);
    a->grenade_cooldown = halo_rand_range(s, 3.0f, 6.0f);
}

static int can_see(HaloSim* s, int ui, int ti, float* out_dist) {
    const HaloUnit* u = &s->units[ui];
    const HaloUnit* t = &s->units[ti];
    const HaloActorDef* ad = &g_halo_actors[s->ai[ui].type];
    hv3 eye = halo_unit_eye(u);
    hv3 c = halo_unit_center(t);
    hv3 to = hv3_sub(c, eye);
    float dist = hv3_len(to);
    if (out_dist) *out_dist = dist;
    if (dist > ad->vision_range) return 0;
    if (dist > AI_PROXIMITY_SENSE) {
        hv3 fwd = hv3_from_angles(u->yaw, 0.0f);
        hv3 flat = hv3_norm(hv3_make(to.x, to.y, 0.0f));
        if (hv3_dot(fwd, flat) < cosf(ad->vision_half_angle)) return 0;
    }
    return halo_line_of_sight(s, eye, c) || halo_line_of_sight(s, eye, halo_unit_eye(t));
}

int halo_ai_pick_target(HaloSim* s, int ui) {
    int best = -1;
    float best_d = 1e9f;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (i == ui || !halo_unit_alive(s, i) || s->units[i].team == s->units[ui].team) continue;
        if (s->units[i].team == HALO_TEAM_NEUTRAL) continue;
        float d;
        if (can_see(s, ui, i, &d) && d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static void perceive(HaloSim* s, int ui) {
    HaloAi* a = &s->ai[ui];
    if (!halo_unit_alive(s, a->target)) {
        a->target = -1;
        a->target_visible = 0;
    }
    int seen = halo_ai_pick_target(s, ui);
    if (seen >= 0 && (a->target < 0 || !a->target_visible)) a->target = seen;
    if (a->target >= 0) {
        a->target_visible = can_see(s, ui, a->target, NULL);
        if (a->target_visible) {
            a->last_known_target = s->units[a->target].pos;
            a->target_visible_time += HALO_DT;
            a->target_lost_time = 0.0f;
        } else {
            a->target_visible_time = 0.0f;
            a->target_lost_time += HALO_DT;
        }
    }
}

static void face_point(HaloUnit* u, hv3 p) {
    hv3 eye = halo_unit_eye(u);
    hv3 d = hv3_sub(p, eye);
    if (hv3_len_xy(d) > 1e-3f) u->control.aim_yaw = atan2f(d.y, d.x);
    u->control.aim_pitch = atan2f(d.z, hc_maxf(hv3_len_xy(d), 1e-3f));
}

static void aim_at_target(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    if (!halo_unit_alive(s, a->target)) return;
    const HaloUnit* t = &s->units[a->target];
    hv3 c = halo_unit_center(t);
    float speed = 20.0f;
    if (u->weapon.id != HALO_WEAPON_NONE) {
        speed = g_halo_projectiles[g_halo_weapons[u->weapon.id].trigger.projectile].initial_velocity;
    }
    float flight = hv3_dist(halo_unit_eye(u), c) / hc_maxf(speed, 1.0f);
    /* Lead about half the motion: Grunts are decent, not perfect. */
    hv3 lead = hv3_mad(c, t->vel, flight * 0.5f);
    face_point(u, lead);
}

static void drive_movement(HaloSim* s, int ui, int face_move) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    const HaloWorldApi* w = s->world;
    if (!a->has_move_goal) return;

    a->repath_timer -= HALO_DT;
    if (w && w->find_path && a->repath_timer <= 0.0f) {
        a->path_count = w->find_path(w->ctx, u->pos, a->move_goal, a->path, HALO_AI_MAX_PATH);
        a->path_index = 0;
        a->repath_timer = AI_REPATH_TIME;
    }
    hv3 wp = a->move_goal;
    while (a->path_count > 0 && a->path_index < a->path_count) {
        wp = a->path[a->path_index];
        if (hv3_dist_xy(u->pos, wp) > AI_ARRIVE_DIST || a->path_index == a->path_count - 1) break;
        a->path_index++;
    }
    if (hv3_dist_xy(u->pos, a->move_goal) < AI_ARRIVE_DIST) {
        a->has_move_goal = 0;
        a->path_count = 0;
        return;
    }
    hv3 d = hv3_sub(wp, u->pos);
    d.z = 0.0f;
    d = hv3_norm(d);
    float cy = cosf(u->yaw), sy = sinf(u->yaw);
    float k = a->move_speed * g_halo_actors[a->type].movement_speed_scale;
    u->control.throttle_forward = (d.x * cy + d.y * sy) * k;
    u->control.throttle_left = (-d.x * sy + d.y * cy) * k;
    if (face_move) {
        u->control.aim_yaw = atan2f(d.y, d.x);
        u->control.aim_pitch = 0.0f;
    }

    /* Stuck: slide around whatever we are pinned against. */
    float moved = hv3_dist_xy(u->pos, a->last_pos);
    if (moved < 0.15f * a->move_speed * HALO_DT * 2.0f) a->stuck_time += HALO_DT;
    else a->stuck_time = hc_maxf(0.0f, a->stuck_time - HALO_DT);
    if (a->stuck_time > 0.7f) {
        a->stuck_time = 0.0f;
        float ang = atan2f(d.y, d.x) + (halo_randf(s) < 0.5f ? 1.0f : -1.0f) * halo_rand_range(s, 1.2f, 2.2f);
        hv3 detour = hv3_make(u->pos.x + cosf(ang) * 1.2f, u->pos.y + sinf(ang) * 1.2f, u->pos.z);
        a->path_count = 1;
        a->path[0] = detour;
        a->path_index = 0;
        a->repath_timer = 0.6f;
    }
}

static void update_firing(HaloSim* s, int ui, float dist) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];
    if (u->weapon.id == HALO_WEAPON_NONE) return;
    const HaloWeaponDef* wd = &g_halo_weapons[u->weapon.id];

    if (a->burst_timer > 0.0f) {
        a->burst_timer -= HALO_DT;
        if (a->burst_timer <= 0.0f) a->burst_cooldown = halo_rand_range(s, ad->burst_separation_min, ad->burst_separation_max);
    } else if (a->burst_cooldown > 0.0f) {
        a->burst_cooldown -= HALO_DT;
    } else if (a->target_visible && dist < ad->combat_range_max + 5.0f) {
        a->burst_timer = halo_rand_range(s, ad->burst_duration_min, ad->burst_duration_max);
    }

    a->semiauto_timer = hc_maxf(a->semiauto_timer - HALO_DT, 0.0f);
    float yaw_err = fabsf(hc_wrap_angle(u->control.aim_yaw - u->yaw));
    int on_target = yaw_err < HC_DEG2RAD(20.0f);
    if (a->burst_timer > 0.0f && on_target && a->target_visible) {
        if (wd->trigger.automatic) {
            u->control.fire = 1;
        } else if (a->semiauto_timer <= 0.0f && !u->weapon.trigger_was_down) {
            u->control.fire = 1;
            a->semiauto_timer = 1.0f / hc_maxf(ad->semiauto_fire_rate, 0.5f);
        }
    }
}

static void update_melee(HaloSim* s, int ui, float dist) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];
    if (!ad->has_melee || !halo_unit_alive(s, a->target)) return;
    a->melee_cooldown = hc_maxf(a->melee_cooldown - HALO_DT, 0.0f);
    float reach = ad->melee_range + halo_unit_def(&s->units[a->target])->collision_radius;
    if (dist < reach && a->melee_cooldown <= 0.0f) {
        HaloDamageEffectDef melee = { HALO_DAMAGE_MELEE, ad->melee_damage, ad->melee_damage, 0, 0, 1.5f, 0 };
        hv3 toward = hv3_norm(hv3_sub(u->pos, s->units[a->target].pos));
        halo_damage_unit(s, a->target, ui, &melee, 1.0f, toward, 0);
        a->melee_cooldown = 1.5f;
    }
}

static void update_combat(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];

    if (!halo_unit_alive(s, a->target)) {
        a->target = halo_ai_pick_target(s, ui);
        if (a->target < 0) {
            a->home = u->pos;
            set_state(s, ui, HALO_AI_IDLE);
            return;
        }
    }
    if (a->target_lost_time > AI_LOSE_TARGET_TIME) {
        set_state(s, ui, HALO_AI_SEARCH);
        return;
    }

    hv3 tp = a->target_visible ? s->units[a->target].pos : a->last_known_target;
    float dist = hv3_dist(u->pos, tp);

    if (!a->target_visible || dist > ad->combat_range_max) {
        set_goal(a, tp, 1.0f);
    } else if (dist < ad->combat_range_min) {
        hv3 away = hv3_norm(hv3_sub(u->pos, tp));
        hv3 goal = hv3_mad(u->pos, away, 1.5f);
        const HaloWorldApi* w = s->world;
        if (!w || !w->can_walk || w->can_walk(w->ctx, u->pos, goal, halo_unit_def(u)->collision_radius)) {
            set_goal(a, goal, 0.8f);
        } else {
            a->has_move_goal = 0;
        }
    } else {
        a->has_move_goal = 0;
        a->strafe_timer -= HALO_DT;
        if (a->strafe_timer <= 0.0f) {
            a->strafe_timer = halo_rand_range(s, 0.6f, 1.4f);
            a->strafe_dir = (halo_randf(s) < ad->strafe_chance) ? (halo_randf(s) < 0.5f ? -1.0f : 1.0f) : 0.0f;
        }
    }

    drive_movement(s, ui, 0);
    if (!a->has_move_goal && a->strafe_dir != 0.0f && dist >= ad->combat_range_min) {
        u->control.throttle_left = a->strafe_dir * 0.75f;
    }

    if (a->target_visible) aim_at_target(s, ui);
    else face_point(u, hv3_make(tp.x, tp.y, tp.z + 0.4f));

    update_firing(s, ui, dist);
    update_melee(s, ui, dist);

    a->grenade_cooldown -= HALO_DT;
    if (a->target_visible && a->grenade_cooldown <= 0.0f && u->grenades[u->grenade_type] > 0 && dist > 3.0f &&
        dist < 9.0f && halo_randf(s) < ad->grenade_chance * HALO_DT) {
        u->control.grenade_pressed = 1;
        a->grenade_cooldown = halo_rand_range(s, 6.0f, 10.0f);
    }
}

void halo_ai_update(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];

    u->control.throttle_forward = 0.0f;
    u->control.throttle_left = 0.0f;
    u->control.fire = 0;
    u->control.crouch = 0;

    if (u->dead) {
        a->state = HALO_AI_DEAD;
        return;
    }
    a->state_time += HALO_DT;
    a->panic_timer = hc_maxf(0.0f, a->panic_timer - HALO_DT);

    perceive(s, ui);

    switch (a->state) {
        case HALO_AI_IDLE:
            if (a->target >= 0 && a->target_visible) {
                set_state(s, ui, HALO_AI_ALERT);
                break;
            }
            a->idle_timer -= HALO_DT;
            if (a->idle_timer <= 0.0f) {
                a->idle_timer = halo_rand_range(s, 2.5f, 6.0f);
                float ang = halo_rand_range(s, -HC_PI, HC_PI);
                float r = halo_rand_range(s, 0.8f, 2.5f);
                hv3 goal = hv3_make(a->home.x + cosf(ang) * r, a->home.y + sinf(ang) * r, a->home.z);
                const HaloWorldApi* w = s->world;
                if (!w || !w->can_walk || w->can_walk(w->ctx, u->pos, goal, halo_unit_def(u)->collision_radius)) {
                    set_goal(a, goal, 0.35f);
                }
            }
            drive_movement(s, ui, 1);
            break;

        case HALO_AI_ALERT:
            face_point(u, hv3_make(a->last_known_target.x, a->last_known_target.y, a->last_known_target.z + 0.4f));
            if (a->state_time >= ad->acknowledge_time) {
                set_state(s, ui, a->target_visible ? HALO_AI_COMBAT : HALO_AI_SEARCH);
            }
            break;

        case HALO_AI_COMBAT:
            update_combat(s, ui);
            break;

        case HALO_AI_SEARCH:
            if (a->target >= 0 && a->target_visible) {
                set_state(s, ui, HALO_AI_COMBAT);
                break;
            }
            set_goal(a, a->last_known_target, 0.7f);
            drive_movement(s, ui, 1);
            if (!a->has_move_goal || a->state_time > AI_SEARCH_TIME) {
                a->home = u->pos;
                a->target = -1;
                set_state(s, ui, HALO_AI_IDLE);
            }
            break;

        case HALO_AI_FLEE: {
            a->flee_timer -= HALO_DT;
            hv3 threat = halo_unit_alive(s, a->target) ? s->units[a->target].pos : a->last_known_target;
            hv3 away = hv3_sub(u->pos, threat);
            away.z = 0.0f;
            if (hv3_len(away) < 1e-3f) away = hv3_from_angles(u->yaw, 0.0f);
            away = hv3_norm(away);
            /* Wobble so fleeing Grunts zig-zag instead of beelining. */
            float wob = sinf(a->state_time * 5.0f + (float)ui) * 0.6f;
            float ang = atan2f(away.y, away.x) + wob;
            set_goal(a, hv3_make(u->pos.x + cosf(ang) * 2.5f, u->pos.y + sinf(ang) * 2.5f, u->pos.z), 1.0f);
            drive_movement(s, ui, 1);
            if (a->flee_timer <= 0.0f) {
                set_state(s, ui, halo_unit_alive(s, a->target) ? HALO_AI_COMBAT : HALO_AI_IDLE);
            }
            break;
        }

        default:
            break;
    }

    a->last_pos = u->pos;
}

void halo_ai_notify_damaged(HaloSim* s, int ui, int attacker) {
    if (!s->ai[ui].active || s->units[ui].dead) return;
    HaloAi* a = &s->ai[ui];
    const HaloActorDef* ad = &g_halo_actors[a->type];
    if (halo_unit_alive(s, attacker) && s->units[attacker].team != s->units[ui].team) {
        a->target = attacker;
        a->last_known_target = s->units[attacker].pos;
        if (a->state == HALO_AI_IDLE || a->state == HALO_AI_ALERT || a->state == HALO_AI_SEARCH) {
            set_state(s, ui, HALO_AI_COMBAT);
        }
    }
    const HaloBipedDef* bd = halo_unit_def(&s->units[ui]);
    float frac = s->units[ui].body / bd->maximum_body_vitality;
    if (a->state != HALO_AI_FLEE && frac < ad->flee_body_fraction && halo_randf(s) < 0.6f) start_flee(s, ui, 1.0f);
}

void halo_ai_notify_killed(HaloSim* s, int ui, int killer) {
    int was_leader = s->ai[ui].active && g_halo_actors[s->ai[ui].type].is_leader;
    if (s->ai[ui].active) s->ai[ui].state = HALO_AI_DEAD;
    hv3 at = s->units[ui].pos;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (i == ui || !s->ai[i].active || !halo_unit_alive(s, i)) continue;
        if (s->units[i].team != s->units[ui].team) continue;
        if (hv3_dist(s->units[i].pos, at) > AI_SQUAD_RADIUS) continue;
        HaloAi* a = &s->ai[i];
        const HaloActorDef* ad = &g_halo_actors[a->type];
        if (halo_unit_alive(s, killer) && s->units[killer].team != s->units[i].team) {
            a->target = killer;
            a->last_known_target = s->units[killer].pos;
            if (a->state == HALO_AI_IDLE) set_state(s, i, HALO_AI_ALERT);
        }
        float chance = was_leader ? ad->panic_chance_leader_killed : ad->panic_chance_friend_killed;
        if (a->state != HALO_AI_FLEE && halo_randf(s) < chance) start_flee(s, i, 1.5f);
    }
}

void halo_ai_notify_noise(HaloSim* s, hv3 pos, float range, int source) {
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (i == source || !s->ai[i].active || !halo_unit_alive(s, i)) continue;
        HaloAi* a = &s->ai[i];
        if (a->state != HALO_AI_IDLE && a->state != HALO_AI_SEARCH) continue;
        const HaloActorDef* ad = &g_halo_actors[a->type];
        if (hv3_dist(s->units[i].pos, pos) > hc_minf(range, ad->hearing_range)) continue;
        int enemy = halo_unit_alive(s, source) && s->units[source].team != s->units[i].team;
        if (enemy) {
            a->target = source;
            a->last_known_target = s->units[source].pos;
        } else if (halo_unit_alive(s, source) && s->ai[source].active && halo_unit_alive(s, s->ai[source].target)) {
            a->target = s->ai[source].target;
            a->last_known_target = s->units[a->target].pos;
        } else {
            a->last_known_target = pos;
        }
        set_state(s, i, HALO_AI_ALERT);
    }
}

void halo_ai_notify_grenade_stuck(HaloSim* s, int ui) {
    if (!s->ai[ui].active || !halo_unit_alive(s, ui)) return;
    start_flee(s, ui, 3.0f);
}
