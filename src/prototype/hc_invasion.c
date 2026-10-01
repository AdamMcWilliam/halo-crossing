#include "hc_invasion.h"

#include "hc_ac_world.h"
#include "hc_world_scale.h"
#include "m_field_make.h"

#define BLOCK_AC 640.0f
#define SQUAD_SPACING 5.0f    /* wu between squad centres */
#define MEMBER_RING 0.8f      /* wu from the centre */
#define SITE_TRIES 40

typedef struct Member {
    HaloActorTypeId type;
    HaloWeaponId weapon;
} Member;

typedef struct Squad {
    Member m[4];
    int count;
} Squad;

/* Loosely CE's Normal-difficulty patrols. */
static const Squad k_squads[] = {
    { { { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL }, { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL },
        { HALO_ACTOR_GRUNT, HALO_WEAPON_NEEDLER } }, 3 },
    { { { HALO_ACTOR_ELITE, HALO_WEAPON_PLASMA_RIFLE }, { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL },
        { HALO_ACTOR_GRUNT, HALO_WEAPON_NEEDLER } }, 3 },
    { { { HALO_ACTOR_ELITE, HALO_WEAPON_NEEDLER }, { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL },
        { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL }, { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL } }, 4 },
    { { { HALO_ACTOR_ELITE, HALO_WEAPON_PLASMA_RIFLE }, { HALO_ACTOR_ELITE, HALO_WEAPON_PLASMA_RIFLE } }, 2 },
    { { { HALO_ACTOR_GRUNT, HALO_WEAPON_FUEL_ROD }, { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL },
        { HALO_ACTOR_GRUNT, HALO_WEAPON_PLASMA_PISTOL } }, 3 },
};
#define SQUAD_KINDS ((int)(sizeof(k_squads) / sizeof(k_squads[0])))

static unsigned int s_rng = 0x51A7E5u;

static float frand(void) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng >> 8) * (1.0f / 16777216.0f);
}

static int count_covenant(const HaloSim* sim) {
    int n = 0;
    for (int i = 0; i < HALO_MAX_UNITS; i++)
        if (sim->units[i].active && !sim->units[i].dead && sim->units[i].team == HALO_TEAM_COVENANT) n++;
    return n;
}

/* A random point on the town's foreground blocks (x 1..5, z 1..6). */
static hv3 random_town_point(void) {
    xyz_t a;
    a.x = BLOCK_AC * (1.0f + frand() * FG_BLOCK_X_NUM);
    a.z = BLOCK_AC * (1.0f + frand() * FG_BLOCK_Z_NUM);
    a.y = 0.0f;
    return hc_a2h_pos(a);
}

int hc_invasion_populate(HaloSim* sim, hv3 avoid, float keep_clear, int squads) {
    s_rng ^= sim->rng ^ (sim->tick * 2654435761u);
    if (!s_rng) s_rng = 0x51A7E5u;
    hv3 centers[32];
    int placed = 0, spawned = 0;
    int budget = HC_INVASION_MAX_COVENANT - count_covenant(sim);
    for (int sq = 0; sq < squads && placed < 32 && budget > 0; sq++) {
        hv3 c;
        int ok = 0;
        for (int t = 0; t < SITE_TRIES && !ok; t++) {
            c = random_town_point();
            if (hv3_len_xy(hv3_sub(c, avoid)) < keep_clear) continue;
            int crowded = 0;
            for (int k = 0; k < placed; k++)
                if (hv3_len_xy(hv3_sub(c, centers[k])) < SQUAD_SPACING) crowded = 1;
            if (crowded) continue;
            ok = hc_ac_world_spawn_ok(&c);
        }
        if (!ok) continue;
        centers[placed++] = c;

        const Squad* s = &k_squads[(int)(frand() * SQUAD_KINDS) % SQUAD_KINDS];
        float facing = frand() * 2.0f * HC_PI;
        for (int m = 0; m < s->count && budget > 0; m++) {
            float a = facing + (float)m * (2.0f * HC_PI / (float)s->count);
            hv3 p = hv3_make(c.x + cosf(a) * MEMBER_RING, c.y + sinf(a) * MEMBER_RING, c.z);
            if (!hc_ac_world_spawn_ok(&p)) p = hv3_make(c.x + cosf(a) * 0.25f, c.y + sinf(a) * 0.25f, c.z);
            int u = halo_spawn_actor(sim, s->m[m].type, p, facing + frand() * 1.5f - 0.75f);
            if (u < 0) break;
            sim->units[u].grounded = 1;
            sim->ai[u].squad = 100 + placed;
            if (s->m[m].weapon != g_halo_actors[s->m[m].type].weapon) halo_set_unit_weapon(sim, u, s->m[m].weapon);
            spawned++;
            budget--;
        }
    }
    return spawned;
}

int hc_invasion_clear(HaloSim* sim) {
    int n = 0;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (sim->units[i].active && sim->units[i].team == HALO_TEAM_COVENANT) {
            halo_remove_unit(sim, i);
            n++;
        }
    }
    return n;
}
