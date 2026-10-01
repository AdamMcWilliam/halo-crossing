/* Headless checks for the Halo sandbox: no host, flat ground, one wall.
 * Build: see tests/run_tests.sh */
#include <stdio.h>
#include <stdlib.h>

#include "halo/halo_sim.h"

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

int main(void) {
    test_movement();
    test_shields();
    test_assault_rifle();
    test_grunt_fight();
    test_plasma_grenade_stick();
    if (g_failures) {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all halo sandbox checks passed\n");
    return 0;
}
