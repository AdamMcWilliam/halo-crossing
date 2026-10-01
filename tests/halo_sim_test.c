/* Headless checks for the Halo sandbox: no host, flat ground, one wall.
 * Build: see tests/run_tests.sh */
#include <stdio.h>
#include <stdlib.h>

#include "halo/halo_internal.h"

static int g_failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            g_failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

/* A wall on the plane x = 20 between y = -2 and y = 2. */
#define WALL_X 20.0f

static void flat_move(void* ctx, hv3 from, hv3 to, float radius, float height, HaloMoveResult* out) {
    (void)ctx;
    (void)height;
    out->position = to;
    out->hit_wall = 0;
    if (from.x < WALL_X - radius && to.x > WALL_X - radius && to.y > -2 && to.y < 2) {
        out->position.x = WALL_X - radius;
        out->hit_wall = 1;
    }
    out->has_ground = 1;
    out->ground_z = 0.0f;
    out->ground_normal = hv3_make(0, 0, 1);
    out->in_water = 0;
}

static int flat_ray(void* ctx, hv3 a, hv3 b, HaloRayHit* hit) {
    (void)ctx;
    float best = 2.0f;
    hv3 n = hv3_make(0, 0, 1);
    if (a.z > 0 && b.z <= 0) {
        best = a.z / (a.z - b.z);
    }
    if ((a.x - WALL_X) * (b.x - WALL_X) < 0) {
        float t = (WALL_X - a.x) / (b.x - a.x);
        float y = a.y + (b.y - a.y) * t;
        float z = a.z + (b.z - a.z) * t;
        if (t < best && y > -2 && y < 2 && z < 3) {
            best = t;
            n = hv3_make(a.x < WALL_X ? -1.0f : 1.0f, 0, 0);
        }
    }
    if (best > 1.0f) return 0;
    hit->fraction = best;
    hit->point = hv3_lerp(a, b, best);
    hit->normal = n;
    hit->is_water = 0;
    return 1;
}

static int flat_ground(void* ctx, hv3 p, float* z) {
    (void)ctx;
    (void)p;
    *z = 0.0f;
    return 1;
}

static int flat_walk(void* ctx, hv3 a, hv3 b, float r) {
    (void)ctx;
    (void)r;
    HaloRayHit h;
    a.z = b.z = 0.3f;
    return !flat_ray(NULL, a, b, &h);
}

static const HaloWorldApi k_flat = { NULL, flat_move, flat_ray, flat_ground, flat_walk, NULL, NULL, NULL };

static void run_seconds(HaloSim* s, float seconds) {
    int ticks = (int)(seconds * HALO_TICKS_PER_SECOND + 0.5f);
    for (int i = 0; i < ticks; i++) {
        halo_sim_tick(s);
        halo_sim_clear_events(s);
    }
}

static void aim_player_at(HaloSim* s, int target) {
    HaloUnit* p = halo_player(s);
    hv3 eye = halo_unit_eye(p);
    hv3 c = s->units[target].pos;
    c.z += halo_unit_height(&s->units[target]) * 0.5f;
    hv3 d = hv3_sub(c, eye);
    p->control.aim_yaw = atan2f(d.y, d.x);
    p->control.aim_pitch = atan2f(d.z, hv3_len_xy(d));
}

static void test_movement(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 1);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    HaloUnit* p = &s.units[pi];
    p->grounded = 1;
    p->control.throttle_forward = 1.0f;
    run_seconds(&s, 1.0f);
    CHECK(fabsf(p->vel.x - 2.25f) < 0.05f, "run speed %.3f, want 2.25", p->vel.x);
    p->control.throttle_forward = 0.0f;
    run_seconds(&s, 0.5f);
    CHECK(fabsf(p->vel.x) < 0.01f, "stops, vel %.3f", p->vel.x);

    p->control.jump_pressed = 1;
    float peak = 0.0f;
    for (int i = 0; i < 60; i++) {
        halo_sim_tick(&s);
        if (p->pos.z > peak) peak = p->pos.z;
    }
    CHECK(peak > 0.25f && peak < 0.4f, "jump apex %.3f wu", peak);
    CHECK(p->grounded && p->pos.z == 0.0f, "landed z=%.3f", p->pos.z);
}

static void test_shields(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 2);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    HaloUnit* p = &s.units[pi];
    HaloDamageEffectDef bolt = { HALO_DAMAGE_PLASMA, 10.0f, 10.0f, 0, 0, 0, 0 };
    halo_damage_unit(&s, pi, -1, &bolt, 1.0f, hv3_make(1, 0, 0), 0);
    CHECK(fabsf(p->shield - 60.0f) < 0.01f, "plasma x1.5 on shields: shield %.2f", p->shield);
    CHECK(p->body == 75.0f, "body untouched %.2f", p->body);
    HaloDamageEffectDef big = { HALO_DAMAGE_BULLET, 100.0f, 100.0f, 0, 0, 0, 0 };
    halo_damage_unit(&s, pi, -1, &big, 1.0f, hv3_make(1, 0, 0), 0);
    CHECK(p->shield == 0.0f, "shield broken");
    CHECK(p->body < 75.0f && p->body > 0.0f, "spill to body, body %.2f", p->body);
    float body_after = p->body;
    run_seconds(&s, 1.9f);
    CHECK(p->shield == 0.0f, "no recharge during stun, shield %.2f", p->shield);
    run_seconds(&s, 2.2f);
    CHECK(p->shield == 75.0f, "recharged, shield %.2f", p->shield);
    CHECK(p->body == body_after, "body never regenerates");
}

static void test_assault_rifle(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 3);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    HaloUnit* p = &s.units[pi];
    CHECK(p->weapon.id == HALO_WEAPON_ASSAULT_RIFLE, "holding AR");
    p->control.fire = 1;
    run_seconds(&s, 1.0f);
    int fired = 60 - p->weapon.rounds_loaded;
    CHECK(fired >= 14 && fired <= 16, "AR fired %d rounds in 1 s", fired);
    CHECK(p->weapon.error > 0.9f, "bloom after 1 s: %.2f", p->weapon.error);
    run_seconds(&s, 3.5f);
    CHECK(p->weapon.reload_timer > 0.0f || p->weapon.rounds_loaded == 60, "auto reload on empty");
    p->control.fire = 0;
    run_seconds(&s, 2.5f);
    CHECK(p->weapon.rounds_loaded == 60, "reloaded, mag %d", p->weapon.rounds_loaded);
    CHECK(p->weapon.rounds_reserve == 120, "reserve %d", p->weapon.rounds_reserve);
}

static void test_grunt_fight(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 4);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int gi = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(9, 0.5f, 0), HC_PI);
    HaloUnit* p = &s.units[pi];
    HaloUnit* g = &s.units[gi];
    p->grounded = g->grounded = 1;

    int saw_combat = 0, grunt_fired = 0;
    for (int i = 0; i < 30 * 6; i++) {
        halo_sim_tick(&s);
        for (int e = 0; e < s.event_count; e++) {
            HaloEvent* ev = &s.events[e];
            if (ev->type == HALO_EV_WEAPON_FIRED && ev->unit == gi) {
                grunt_fired++;
                if (getenv("HALO_TEST_VERBOSE"))
                    printf("t=%.2f grunt fires from (%.2f %.2f %.2f) dir (%.2f %.2f %.2f) yaw %.1f\n", s.time, ev->pos.x,
                           ev->pos.y, ev->pos.z, ev->dir.x, ev->dir.y, ev->dir.z, HC_RAD2DEG(g->yaw));
            }
            if (ev->type == HALO_EV_PROJECTILE_IMPACT && getenv("HALO_TEST_VERBOSE"))
                printf("t=%.2f impact at (%.2f %.2f %.2f) unit %d\n", s.time, ev->pos.x, ev->pos.y, ev->pos.z,
                       ev->other);
        }
        halo_sim_clear_events(&s);
        if (s.ai[gi].state == HALO_AI_COMBAT) saw_combat = 1;
    }
    CHECK(saw_combat, "grunt entered combat (state %s)", halo_ai_state_name(s.ai[gi].state));
    CHECK(grunt_fired > 0, "grunt fired %d bolts", grunt_fired);
    float d = hv3_dist(p->pos, g->pos);
    CHECK(d < 9.0f, "grunt moved into its combat band, dist %.2f", d);
    CHECK(p->shield < 75.0f || p->body < 75.0f, "grunt hit the player (shield %.1f)", p->shield);

    int killed = 0;
    for (int i = 0; i < 30 * 6 && !killed; i++) {
        aim_player_at(&s, gi);
        p->control.fire = 1;
        halo_sim_tick(&s);
        for (int e = 0; e < s.event_count; e++) {
            if (s.events[e].type == HALO_EV_UNIT_KILLED && s.events[e].unit == gi) killed = 1;
        }
        halo_sim_clear_events(&s);
    }
    CHECK(killed, "grunt died to AR fire (body %.1f, state %s)", g->body, halo_ai_state_name(s.ai[gi].state));
    CHECK(halo_count_living(&s, HALO_TEAM_COVENANT) == 0, "no covenant left");
}

static void test_plasma_grenade_stick(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 5);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int gi = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(1.2f, 0, 0), HC_PI);
    s.ai_frozen = 1;
    HaloUnit* p = &s.units[pi];
    p->grounded = s.units[gi].grounded = 1;
    aim_player_at(&s, gi);
    p->control.aim_pitch -= HC_DEG2RAD(12.0f);
    p->control.grenade_pressed = 1;
    int stuck = 0, boom = 0;
    for (int i = 0; i < 30 * 5; i++) {
        halo_sim_tick(&s);
        for (int e = 0; e < s.event_count; e++) {
            if (s.events[e].type == HALO_EV_GRENADE_STUCK && s.events[e].other == gi) stuck = 1;
            if (s.events[e].type == HALO_EV_EXPLOSION) boom = 1;
        }
        halo_sim_clear_events(&s);
    }
    CHECK(stuck, "plasma grenade stuck to the grunt");
    CHECK(boom, "plasma grenade detonated");
    CHECK(s.units[gi].dead, "stuck grunt died");
}

static void hold_weapon(HaloSim* s, HaloWeaponId w) {
    HaloUnit* p = halo_player(s);
    p->control.weapon_select = w + 1;
    halo_sim_tick(s);
    run_seconds(s, g_halo_weapons[w].ready_time + 0.1f);
}

static int count_events(HaloSim* s, HaloEventType type, int def) {
    int n = 0;
    for (int e = 0; e < s->event_count; e++)
        if (s->events[e].type == type && (def < 0 || s->events[e].def == def)) n++;
    return n;
}

static void test_arsenal(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 6);
    s.arsenal_enabled = 1;
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    HaloUnit* p = &s.units[pi];
    int owned = 0;
    for (int w = 0; w < HALO_WEAPON_COUNT; w++) owned += s.arsenal_owned[w];
    CHECK(owned == HALO_WEAPON_COUNT, "owns %d of %d weapons", owned, HALO_WEAPON_COUNT);
    CHECK(p->weapon.id == HALO_WEAPON_ASSAULT_RIFLE, "spawns holding the AR");
    CHECK(p->grenades[HALO_GRENADE_FRAG] == 4 && p->grenades[HALO_GRENADE_PLASMA] == 4, "full grenades");

    p->control.fire = 1;
    run_seconds(&s, 0.5f);
    p->control.fire = 0;
    int ar_loaded = p->weapon.rounds_loaded;
    CHECK(ar_loaded < 60, "AR fired, mag %d", ar_loaded);

    p->control.weapon_cycle = 1;
    halo_sim_tick(&s);
    CHECK(p->weapon.id == HALO_WEAPON_PISTOL, "wheel down -> pistol (got %d)", p->weapon.id);
    CHECK(p->weapon.ready_timer > 0.0f, "drawing the pistol");
    p->control.weapon_cycle = -1;
    halo_sim_tick(&s);
    p->control.weapon_cycle = -1;
    halo_sim_tick(&s);
    CHECK(p->weapon.id == HALO_WEAPON_FUEL_ROD, "wheel up wraps to the fuel rod (got %d)", p->weapon.id);
    p->control.weapon_select = HALO_WEAPON_NEEDLER + 1;
    halo_sim_tick(&s);
    CHECK(p->weapon.id == HALO_WEAPON_NEEDLER, "number key selects the needler");
    p->control.weapon_select = HALO_WEAPON_ASSAULT_RIFLE + 1;
    halo_sim_tick(&s);
    CHECK(p->weapon.rounds_loaded == ar_loaded, "AR magazine kept while holstered (%d vs %d)",
          p->weapon.rounds_loaded, ar_loaded);
    p->control.swap_pressed = 1;
    halo_sim_tick(&s);
    CHECK(p->weapon.id == HALO_WEAPON_NEEDLER, "swap returns to the last weapon (got %d)", p->weapon.id);

    halo_kill_unit(&s, pi, -1);
    run_seconds(&s, 3.5f);
    p = halo_player(&s);
    owned = 0;
    for (int w = 0; w < HALO_WEAPON_COUNT; w++) owned += s.arsenal_owned[w];
    CHECK(p && !p->dead && owned == HALO_WEAPON_COUNT, "respawn keeps the arsenal");
    CHECK(p && s.arsenal[HALO_WEAPON_ASSAULT_RIFLE].rounds_loaded == 60, "respawn refills");
}

static void test_shotgun(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 7);
    s.arsenal_enabled = 1;
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int gi = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(1.5f, 0, 0), HC_PI);
    s.ai_frozen = 1;
    HaloUnit* p = &s.units[pi];
    hold_weapon(&s, HALO_WEAPON_SHOTGUN);
    CHECK(p->weapon.id == HALO_WEAPON_SHOTGUN, "holding the shotgun");
    aim_player_at(&s, gi);
    p->control.fire = 1;
    halo_sim_tick(&s);
    /* Pellets cover 5 wu a tick, so most have already landed: count both. */
    int pellets = count_events(&s, HALO_EV_PROJECTILE_IMPACT, HALO_PROJ_SHOTGUN_PELLET);
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++)
        if (s.projectiles[i].active && s.projectiles[i].def == HALO_PROJ_SHOTGUN_PELLET) pellets++;
    halo_sim_clear_events(&s);
    p->control.fire = 0;
    CHECK(pellets >= 10, "one shell spawned %d pellets", pellets);
    run_seconds(&s, 0.3f);
    CHECK(s.units[gi].dead, "point-blank shotgun kills a grunt (body %.1f)", s.units[gi].body);
    CHECK(p->weapon.rounds_loaded == 11, "one shell spent (%d)", p->weapon.rounds_loaded);

    p->control.reload_pressed = 1;
    run_seconds(&s, 0.5f);
    CHECK(p->weapon.rounds_loaded == 12, "shell-by-shell reload (%d)", p->weapon.rounds_loaded);
}

static void test_rocket(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 8);
    s.arsenal_enabled = 1;
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int ei = halo_spawn_actor(&s, HALO_ACTOR_ELITE, hv3_make(6, 0, 0), HC_PI);
    int gi = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(6, 1.0f, 0), HC_PI);
    s.ai_frozen = 1;
    HaloUnit* p = &s.units[pi];
    hold_weapon(&s, HALO_WEAPON_ROCKET_LAUNCHER);
    aim_player_at(&s, ei);
    p->control.fire = 1;
    int boom = 0;
    for (int i = 0; i < 30 * 2; i++) {
        halo_sim_tick(&s);
        p->control.fire = 0;
        boom += count_events(&s, HALO_EV_EXPLOSION, HALO_PROJ_ROCKET);
        halo_sim_clear_events(&s);
    }
    CHECK(boom == 1, "rocket exploded once (%d)", boom);
    CHECK(s.units[ei].dead, "direct rocket kills an elite (shield %.1f body %.1f)", s.units[ei].shield,
          s.units[ei].body);
    CHECK(s.units[gi].dead, "splash kills the grunt next to it");
    CHECK(p->body == 75.0f && p->shield == 75.0f, "shooter untouched at 6 wu");
}

static void test_needler_supercombine(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 9);
    s.arsenal_enabled = 1;
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int ei = halo_spawn_actor(&s, HALO_ACTOR_ELITE, hv3_make(4, 0, 0), HC_PI);
    s.ai_frozen = 1;
    HaloUnit* p = &s.units[pi];
    hold_weapon(&s, HALO_WEAPON_NEEDLER);
    int super = 0, killed = 0;
    for (int i = 0; i < 30 * 3 && !killed; i++) {
        aim_player_at(&s, ei);
        p->control.fire = 1;
        halo_sim_tick(&s);
        for (int e = 0; e < s.event_count; e++) {
            HaloEvent* ev = &s.events[e];
            if (ev->type == HALO_EV_EXPLOSION && ev->def == HALO_PROJ_NEEDLE && ev->value > 0.0f) super++;
            if (ev->type == HALO_EV_UNIT_KILLED && ev->unit == ei) killed = 1;
        }
        halo_sim_clear_events(&s);
    }
    CHECK(super >= 1, "needles supercombined (%d)", super);
    CHECK(killed, "needler kills an elite");
}

static void test_frag_bounce(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 10);
    s.arsenal_enabled = 1;
    int pi = halo_spawn_player(&s, hv3_make(WALL_X - 2.5f, 0, 0), 0.0f);
    HaloUnit* p = &s.units[pi];
    p->grenade_type = HALO_GRENADE_FRAG;
    p->control.aim_pitch = 0.0f;
    p->control.grenade_pressed = 1;
    int frag = -1, bounced_back = 0, rested = 0, boom = 0;
    for (int i = 0; i < 30 * 4 && !boom; i++) {
        halo_sim_tick(&s);
        boom += count_events(&s, HALO_EV_EXPLOSION, HALO_PROJ_FRAG_GRENADE);
        halo_sim_clear_events(&s);
        for (int k = 0; k < HALO_MAX_PROJECTILES && frag < 0; k++)
            if (s.projectiles[k].active && s.projectiles[k].def == HALO_PROJ_FRAG_GRENADE) frag = k;
        if (frag >= 0 && s.projectiles[frag].active) {
            if (s.projectiles[frag].vel.x < -0.1f) bounced_back = 1;
            if (s.projectiles[frag].stuck) rested = 1;
        }
    }
    CHECK(frag >= 0, "frag thrown");
    CHECK(p->grenades[HALO_GRENADE_FRAG] == 3, "one frag used (%d)", p->grenades[HALO_GRENADE_FRAG]);
    CHECK(bounced_back, "frag bounced off the wall");
    CHECK(rested, "frag came to rest");
    CHECK(boom == 1, "frag exploded on its fuse");
    CHECK(s.time > 2.1f && s.time < 2.7f, "fuse ~2.2 s (exploded at %.2f)", s.time);
}

static void test_villager_proxy(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 11);
    int pi = halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int vi = halo_spawn_proxy(&s, HALO_BIPED_VILLAGER, HALO_TEAM_NEUTRAL, hv3_make(5, 0, 0), HC_PI);
    int gi = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(8, 0, 0), HC_PI);
    HaloUnit* p = &s.units[pi];
    CHECK(vi >= 0 && s.units[vi].kinematic, "proxy spawned");
    /* Put the player out of the grunt's sight so the villager is the only thing in view. */
    p->pos = p->prev_pos = hv3_make(-30, 0, 0);
    run_seconds(&s, 2.0f);
    CHECK(s.ai[gi].target != vi, "covenant ignore bystanders");
    CHECK(s.units[vi].pos.x == 5.0f, "proxy is not moved by the sim");

    p->pos = p->prev_pos = hv3_make(0, 2.0f, 0);
    s.ai_frozen = 1;
    int killed = 0;
    for (int i = 0; i < 30 * 3 && !killed; i++) {
        aim_player_at(&s, vi);
        p->control.fire = 1;
        halo_sim_tick(&s);
        for (int e = 0; e < s.event_count; e++)
            if (s.events[e].type == HALO_EV_UNIT_KILLED && s.events[e].unit == vi) killed = 1;
        halo_sim_clear_events(&s);
    }
    CHECK(killed, "villager can be shot");
    p->control.fire = 0;
    run_seconds(&s, 50.0f);
    CHECK(s.units[vi].active && s.units[vi].kinematic, "downed villager is never cleaned up");
    halo_revive_unit(&s, vi);
    CHECK(!s.units[vi].dead && s.units[vi].body == 40.0f, "villager gets back up");
}

static void test_dormancy(void) {
    HaloSim s;
    halo_sim_init(&s, &k_flat, 12);
    s.ai_activation_range = 20.0f;
    halo_spawn_player(&s, hv3_make(0, 0, 0), 0.0f);
    int far = halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(60, 30, 0), 0.0f);
    halo_spawn_actor(&s, HALO_ACTOR_GRUNT, hv3_make(8, 0, 0), HC_PI);
    halo_sim_tick(&s);
    CHECK(s.dormant_count == 1, "far squad sleeps (%d dormant)", s.dormant_count);
    halo_ai_notify_noise(&s, hv3_make(58, 30, 0), 20.0f, -1);
    halo_sim_tick(&s);
    CHECK(s.ai[far].state != HALO_AI_IDLE, "noise wakes a sleeper");
}

int main(void) {
    test_movement();
    test_shields();
    test_assault_rifle();
    test_grunt_fight();
    test_plasma_grenade_stick();
    test_arsenal();
    test_shotgun();
    test_rocket();
    test_needler_supercombine();
    test_frag_bounce();
    test_villager_proxy();
    test_dormancy();
    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all halo sandbox checks passed\n");
    return 0;
}
