#include "halo_internal.h"

#include <string.h>

#define HALO_PLAYER_RESPAWN_TIME 3.0f
#define HALO_CORPSE_TIME 45.0f

void halo_sim_init(HaloSim* s, const HaloWorldApi* world, unsigned int seed) {
    memset(s, 0, sizeof(*s));
    s->world = world;
    s->rng = seed ? seed : 0xC0FFEEu;
    s->player = -1;
}

hv3 halo_random_cone(HaloSim* s, hv3 dir, float half_angle) {
    if (half_angle <= 0.0f) return dir;
    /* Uniform in deflection angle (not area), so shots cluster toward the
     * center with occasional strays, like Halo's projectile error. */
    float ang = halo_randf(s) * half_angle;
    float cz = cosf(ang);
    float sz = sinf(ang);
    float phi = halo_randf(s) * 2.0f * HC_PI;
    hv3 up = fabsf(dir.z) < 0.99f ? hv3_make(0, 0, 1) : hv3_make(1, 0, 0);
    hv3 t1 = hv3_norm(hv3_cross(dir, up));
    hv3 t2 = hv3_cross(dir, t1);
    hv3 r = hv3_scale(dir, cz);
    r = hv3_mad(r, t1, sz * cosf(phi));
    r = hv3_mad(r, t2, sz * sinf(phi));
    return r;
}

void halo_emit(HaloSim* s, HaloEventType type, hv3 pos, hv3 dir, int unit, int other, int def, float value) {
    if (s->event_count >= HALO_MAX_EVENTS) {
        s->events_dropped++;
        return;
    }
    HaloEvent* e = &s->events[s->event_count++];
    e->type = type;
    e->pos = pos;
    e->dir = dir;
    e->unit = unit;
    e->other = other;
    e->def = def;
    e->value = value;
}

void halo_sim_clear_events(HaloSim* s) {
    s->event_count = 0;
}

const HaloBipedDef* halo_unit_def(const HaloUnit* u) {
    return &g_halo_bipeds[u->biped];
}

float halo_unit_height(const HaloUnit* u) {
    const HaloBipedDef* d = halo_unit_def(u);
    return d->standing_collision_height +
           (d->crouching_collision_height - d->standing_collision_height) * u->crouch_blend;
}

static float unit_camera_height(const HaloUnit* u) {
    const HaloBipedDef* d = halo_unit_def(u);
    return d->standing_camera_height + (d->crouching_camera_height - d->standing_camera_height) * u->crouch_blend;
}

hv3 halo_unit_eye(const HaloUnit* u) {
    return hv3_make(u->pos.x, u->pos.y, u->pos.z + unit_camera_height(u));
}

hv3 halo_unit_pos_interp(const HaloUnit* u, float alpha) {
    return hv3_lerp(u->prev_pos, u->pos, alpha);
}

hv3 halo_unit_eye_interp(const HaloUnit* u, float alpha) {
    hv3 p = halo_unit_pos_interp(u, alpha);
    p.z += unit_camera_height(u);
    return p;
}

HaloUnit* halo_player(HaloSim* s) {
    return (s->player >= 0 && s->units[s->player].active) ? &s->units[s->player] : NULL;
}

static int alloc_unit(HaloSim* s) {
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (!s->units[i].active) return i;
    }
    /* Recycle the oldest corpse. */
    int best = -1;
    float best_time = -1.0f;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (s->units[i].dead && !s->units[i].is_player && s->units[i].dead_time > best_time) {
            best = i;
            best_time = s->units[i].dead_time;
        }
    }
    if (best >= 0) halo_remove_unit(s, best);
    return best;
}

static int spawn_unit(HaloSim* s, HaloBipedId biped, HaloTeam team, hv3 pos, float yaw) {
    int i = alloc_unit(s);
    if (i < 0) return -1;
    HaloUnit* u = &s->units[i];
    memset(u, 0, sizeof(*u));
    const HaloBipedDef* d = &g_halo_bipeds[biped];
    u->active = 1;
    u->serial = ++s->next_serial;
    u->biped = biped;
    u->team = team;
    u->pos = u->prev_pos = pos;
    u->yaw = u->prev_yaw = yaw;
    u->control.aim_yaw = yaw;
    u->body = d->maximum_body_vitality;
    u->shield = d->maximum_shield_vitality;
    u->last_attacker = -1;
    u->weapon.id = HALO_WEAPON_NONE;
    u->holstered.id = HALO_WEAPON_NONE;
    s->ai[i].active = 0;
    return i;
}

int halo_spawn_player(HaloSim* s, hv3 pos, float yaw) {
    if (s->player >= 0 && s->units[s->player].active) halo_remove_unit(s, s->player);
    int i = spawn_unit(s, HALO_BIPED_CYBORG, HALO_TEAM_HUMAN, pos, yaw);
    if (i < 0) return -1;
    HaloUnit* u = &s->units[i];
    u->is_player = 1;
    halo_give_weapon(s, i, HALO_WEAPON_ASSAULT_RIFLE);
    halo_give_weapon(s, i, HALO_WEAPON_PLASMA_PISTOL);
    /* Start holding the rifle; the pistol goes to the back. */
    if (u->weapon.id != HALO_WEAPON_ASSAULT_RIFLE) {
        HaloWeaponState t = u->weapon;
        u->weapon = u->holstered;
        u->holstered = t;
    }
    u->weapon.ready_timer = 0.0f;
    u->grenades[HALO_GRENADE_PLASMA] = 2;
    s->player = i;
    s->player_spawn = pos;
    s->player_spawn_yaw = yaw;
    s->player_respawn_timer = 0.0f;
    return i;
}

int halo_spawn_actor(HaloSim* s, HaloActorTypeId type, hv3 pos, float yaw) {
    const HaloActorDef* a = &g_halo_actors[type];
    int i = spawn_unit(s, a->biped, HALO_TEAM_COVENANT, pos, yaw);
    if (i < 0) return -1;
    HaloUnit* u = &s->units[i];
    if (a->weapon != HALO_WEAPON_NONE) halo_weapon_init(&u->weapon, a->weapon);
    u->grenades[HALO_GRENADE_PLASMA] = a->plasma_grenades;
    halo_ai_spawned(s, i, type);
    return i;
}

void halo_give_weapon(HaloSim* s, int ui, HaloWeaponId weapon) {
    if (ui < 0 || !s->units[ui].active || weapon == HALO_WEAPON_NONE) return;
    HaloUnit* u = &s->units[ui];
    HaloWeaponState* slot;
    if (u->weapon.id == weapon) {
        slot = &u->weapon;
    } else if (u->holstered.id == weapon) {
        slot = &u->holstered;
    } else if (u->weapon.id == HALO_WEAPON_NONE) {
        slot = &u->weapon;
    } else if (u->holstered.id == HALO_WEAPON_NONE) {
        slot = &u->holstered;
    } else {
        slot = &u->weapon; /* replace the held weapon, Halo-style */
    }
    halo_weapon_init(slot, weapon);
    if (slot == &u->weapon) u->weapon.ready_timer = g_halo_weapons[weapon].ready_time;
}

void halo_kill_unit(HaloSim* s, int ui, int killer) {
    if (!halo_unit_alive(s, ui)) return;
    HaloUnit* u = &s->units[ui];
    u->dead = 1;
    u->dead_time = 0.0f;
    u->body = 0.0f;
    u->shield = 0.0f;
    u->control.fire = 0;
    u->grenade_timer = 0.0f;
    halo_emit(s, HALO_EV_UNIT_KILLED, halo_unit_center(u), hv3_make(0, 0, 0), ui, killer, u->biped, 0.0f);
    halo_ai_notify_killed(s, ui, killer);
    if (u->is_player) s->player_respawn_timer = HALO_PLAYER_RESPAWN_TIME;
}

void halo_remove_unit(HaloSim* s, int ui) {
    if (ui < 0 || ui >= HALO_MAX_UNITS) return;
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++) {
        HaloProjectile* p = &s->projectiles[i];
        if (p->active && p->attached_unit == ui) {
            p->attached_unit = -1;
            p->stuck = 0;
        }
    }
    s->units[ui].active = 0;
    s->ai[ui].active = 0;
    if (s->player == ui) s->player = -1;
}

int halo_count_living(const HaloSim* s, HaloTeam team) {
    int n = 0;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (s->units[i].active && !s->units[i].dead && s->units[i].team == team) n++;
    }
    return n;
}

static void update_unit(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    if (u->dead) {
        u->dead_time += HALO_DT;
        /* Corpses still fall and slide to rest. */
        u->control.throttle_forward = u->control.throttle_left = 0.0f;
        u->control.jump_pressed = 0;
        halo_biped_update(s, ui);
        u->hurt_flash = hc_maxf(0.0f, u->hurt_flash - HALO_DT * 3.0f);
        u->shield_flash = 0.0f;
        return;
    }

    /* Players aim instantly (mouse); AI turn at a capped rate. */
    if (u->is_player) {
        u->yaw = u->control.aim_yaw;
        u->pitch = u->control.aim_pitch;
    } else {
        float turn = HC_DEG2RAD(540.0f) * HALO_DT;
        float dy = hc_wrap_angle(u->control.aim_yaw - u->yaw);
        u->yaw = hc_wrap_angle(u->yaw + hc_clampf(dy, -turn, turn));
        u->pitch = hc_approachf(u->pitch, u->control.aim_pitch, turn);
    }
    u->pitch = hc_clampf(u->pitch, HC_DEG2RAD(-85.0f), HC_DEG2RAD(85.0f));

    halo_biped_update(s, ui);
    halo_weapon_update(s, ui);
    halo_damage_update(s, ui);

    u->control.jump_pressed = 0;
    u->control.reload_pressed = 0;
    u->control.grenade_pressed = 0;
    u->control.swap_pressed = 0;
    u->last_damage_age += HALO_DT;
}

void halo_sim_tick(HaloSim* s) {
    s->tick++;
    s->time += HALO_DT;

    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        HaloUnit* u = &s->units[i];
        if (!u->active) continue;
        u->prev_pos = u->pos;
        u->prev_yaw = u->yaw;
    }
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++) {
        if (s->projectiles[i].active) s->projectiles[i].prev_pos = s->projectiles[i].pos;
    }

    if (!s->ai_frozen) {
        for (int i = 0; i < HALO_MAX_UNITS; i++) {
            if (s->units[i].active && s->ai[i].active) halo_ai_update(s, i);
        }
    }

    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (s->units[i].active) update_unit(s, i);
    }

    halo_projectiles_update(s);

    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        HaloUnit* u = &s->units[i];
        if (u->active && u->dead && !u->is_player && u->dead_time > HALO_CORPSE_TIME) halo_remove_unit(s, i);
    }

    if (s->player >= 0 && s->units[s->player].dead) {
        s->player_respawn_timer -= HALO_DT;
        if (s->player_respawn_timer <= 0.0f) halo_spawn_player(s, s->player_spawn, s->player_spawn_yaw);
    }
}

int halo_sim_advance(HaloSim* s, float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;
    s->accumulator += dt;
    int ticks = 0;
    while (s->accumulator >= HALO_DT && ticks < HALO_MAX_TICKS_PER_FRAME) {
        halo_sim_tick(s);
        s->accumulator -= HALO_DT;
        ticks++;
    }
    if (s->accumulator > HALO_DT) s->accumulator = HALO_DT;
    s->alpha = hc_clampf(s->accumulator / HALO_DT, 0.0f, 1.0f);
    return ticks;
}

const char* halo_ai_state_name(HaloAiState st) {
    static const char* names[HALO_AI_STATE_COUNT] = { "idle", "alert", "combat", "search", "flee", "dead" };
    return (st >= 0 && st < HALO_AI_STATE_COUNT) ? names[st] : "?";
}
