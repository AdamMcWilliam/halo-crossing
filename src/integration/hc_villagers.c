#include "hc_villagers.h"

#include <stdio.h>
#include <string.h>

#include "hc_world_scale.h"
#include "m_actor.h"

#define HV_MAX 24
#define HV_HEAR_GUNFIRE 7.0f      /* wu */
#define HV_HEAR_EXPLOSION 12.0f
#define HV_SEE_COVENANT 5.0f
#define HV_RUN_SPEED 2.4f         /* wu/s, a bit slower than the Chief */
#define HV_PANIC_TIME_MIN 3.5f
#define HV_PANIC_TIME_MAX 6.0f
#define HV_HIDE_TIME 4.0f
#define HV_DOWN_TIME 20.0f
#define HV_LINE_TIME 2.4f
#define HV_SPECIAL_BODY 1.0e6f    /* scripted NPCs can't go down */
#define HV_NOOK_BODY 150.0f       /* a rocket, a sniper headshot, or about twenty AR rounds */
#define HV_NOOK_DOWN_TIME 1.0e9f  /* stays down until the scene reloads */
#define HV_HEAD_AC 52.0f          /* speech bubble height above the feet */
#define HV_NOOK_RGB 0xFFD08000

typedef struct HcVillager {
    int used;
    ACTOR* actor;
    mActor_name_t npc_id;
    int special;
    int nook;
    s16 down_yaw;
    int unit;
    HcVillagerState state;
    float state_time;
    float state_len;
    hv3 threat;
    float flee_yaw;
    xyz_t puppet;
    hv3 last_pos;
    char line[48];
    float line_age;
    u32 line_rgb;
    float line_cooldown;
} HcVillager;

static HcVillager s_v[HV_MAX];
static unsigned int s_pick = 0x9E37u;
static int s_downed_total;
static char s_summary[64];
static int s_nook_kills;       /* survives scene changes, unlike everything above */
static int s_nook_kill_pending;
static xyz_t s_nook_kill_pos;

/* ---- lines ----------------------------------------------------------------- */

static const char* k_hurt[] = {
    "OW! My fur!",           "Hey! That stings!",      "Not the face!",
    "Is that a new bug net?!", "What did I do to you?!", "I just paid off my loan!",
    "Ouch! Rude!",           "That's not how you fish!",
};
static const char* k_panic[] = {
    "AAAAH! Space people!",   "Run for the museum!",     "I'm too fluffy to die!",
    "Somebody call the cops!", "They're stomping my tulips!", "Not on a weekday!!",
    "Every critter for itself!", "I left the stove on!",
};
static const char* k_alert[] = { "Hm? Thunder?", "Was that fireworks?", "!", "...?" };
static const char* k_hide[] = {
    "I'm a bush. Rustle rustle.", "If I hold still they can't see me...", "Hiding... very... quietly...",
    "Mom, come get me...",
};
static const char* k_down[] = {
    "x_x",                       "Tell everyone... I owe them a peach...", "I see... a giant bell...",
    "Was it... something I said?", "Ugh... the stars...",                 "Put that... in my letter...",
};
static const char* k_getup[] = {
    "I'm OK! I'm OK!", "Just a scratch!", "Did anybody see that?", "I need a nap. And a lawyer.",
};
static const char* k_covenant[] = {
    "You're not on the move-in list!", "Your mask is SO cute... AAH!", "Wort wort to you too?!",
    "Is that a costume?!",
};
static const char* k_special[] = {
    "No shooting near the merchandise!", "That goes on your loan, yes yes!", "Sir, this is a place of business!",
    "Please put that away, hm?",         "I'm calling the town hall!",
};
static const char* k_nook_hurt[] = {
    "Hm? Hey! HEY!",           "That's going on your loan, yes yes!", "Is this about the mortgage?!",
    "No refunds! NO REFUNDS!", "Customer service is CLOSED!",        "I'll double your interest!",
    "Not in front of the register!", "Security! SECURITY!",
};
static const char* k_nook_down[] = {
    "Yes... yes... no...",          "My... bells...",          "Who will... pay for... the renovations...",
    "You still owe me... 39,800 Bells...", "The store... is closed... forever...",
};
static const char* k_nook_back[] = {
    "Welcome back! I have excellent insurance, yes yes!", "Ah, you again. Prices just went up, hm?",
    "Hm? Oh, a customer! Totally unrelated to last time!",
};

#define PICK(arr) pick_line(arr, (int)(sizeof(arr) / sizeof(arr[0])))

static const char* pick_line(const char* const* arr, int n) {
    s_pick = s_pick * 1103515245u + 12345u;
    return arr[(s_pick >> 16) % (unsigned)n];
}

static float hv_rand(void) {
    s_pick = s_pick * 1103515245u + 12345u;
    return (float)((s_pick >> 8) & 0xFFFF) / 65536.0f;
}

static void say(HcVillager* v, const char* text, u32 rgb, int force) {
    if (!force && v->line_cooldown > 0.0f) return;
    snprintf(v->line, sizeof(v->line), "%s", text);
    v->line_age = 0.0f;
    v->line_rgb = rgb;
    v->line_cooldown = 1.2f;
}

/* ---- adoption ----------------------------------------------------------- */

static int s_treat_all;

void hc_villagers_set_treat_all(int on) {
    s_treat_all = on;
}

static int is_villager_profile(s16 id) {
    return s_treat_all || id == mAc_PROFILE_NPC || id == mAc_PROFILE_NPC2 || id == mAc_PROFILE_NORMAL_NPC;
}

/* Every storefront Tom Nook runs, plus his outdoor intro self. */
static int is_nook_profile(s16 id) {
    return id == mAc_PROFILE_NPC_SHOP_MASTER || id == mAc_PROFILE_NPC_SHOP_MASTERSP ||
           id == mAc_PROFILE_NPC_CONV_MASTER || id == mAc_PROFILE_NPC_SUPER_MASTER ||
           id == mAc_PROFILE_NPC_DEPART_MASTER;
}

void hc_villagers_reset(void) {
    memset(s_v, 0, sizeof(s_v));
    s_downed_total = 0;
}

static void release(HcVillager* v) {
    if (v->actor) {
        v->actor->shape_info.rotation.x = 0;
        v->actor->shape_info.rotation.z = 0;
    }
}

static void drop(HaloSim* sim, HcVillager* v) {
    if (v->unit >= 0 && sim->units[v->unit].active && sim->units[v->unit].kinematic) halo_remove_unit(sim, v->unit);
    memset(v, 0, sizeof(*v));
}

static HcVillager* find(ACTOR* a) {
    for (int i = 0; i < HV_MAX; i++)
        if (s_v[i].used && s_v[i].actor == a && s_v[i].npc_id == a->npc_id) return &s_v[i];
    return NULL;
}

static HcVillager* adopt(HaloSim* sim, ACTOR* a) {
    for (int i = 0; i < HV_MAX; i++) {
        HcVillager* v = &s_v[i];
        if (v->used) continue;
        hv3 p = hc_a2h_pos(a->world.position);
        int u = halo_spawn_proxy(sim, HALO_BIPED_VILLAGER, HALO_TEAM_NEUTRAL, p, hc_a2h_yaw(a->shape_info.rotation.y));
        if (u < 0) return NULL;
        memset(v, 0, sizeof(*v));
        v->used = 1;
        v->actor = a;
        v->npc_id = a->npc_id;
        v->nook = is_nook_profile(a->id);
        v->special = !v->nook && !is_villager_profile(a->id);
        v->unit = u;
        v->last_pos = p;
        v->puppet = a->world.position;
        if (v->special) sim->units[u].body = HV_SPECIAL_BODY;
        if (v->nook) {
            sim->units[u].body = HV_NOOK_BODY;
            if (s_nook_kills > 0) say(v, PICK(k_nook_back), HV_NOOK_RGB, 1);
        }
        return v;
    }
    return NULL;
}

static void set_state(HcVillager* v, HcVillagerState s, float len) {
    if (v->state == s) return;
    if (s == HC_VILLAGER_NORMAL) release(v);
    if ((s == HC_VILLAGER_PANICKING || s == HC_VILLAGER_DOWN) && v->state != HC_VILLAGER_PANICKING &&
        v->state != HC_VILLAGER_DOWN && v->actor)
        v->puppet = v->actor->world.position;
    if (s == HC_VILLAGER_DOWN && v->actor) v->down_yaw = v->actor->shape_info.rotation.y;
    v->state = s;
    v->state_time = 0.0f;
    v->state_len = len;
}

static void panic_from(HcVillager* v, hv3 threat, const char* line) {
    if (v->special || v->state == HC_VILLAGER_DOWN) {
        if (v->special && line) say(v, PICK(k_special), 0xFFE08000, 0);
        return;
    }
    v->threat = threat;
    if (v->state != HC_VILLAGER_PANICKING) {
        set_state(v, HC_VILLAGER_PANICKING, HV_PANIC_TIME_MIN + hv_rand() * (HV_PANIC_TIME_MAX - HV_PANIC_TIME_MIN));
        if (line) say(v, line, 0xFFF0A000, 0);
    } else {
        v->state_time = 0.0f; /* fresh scare: keep running */
    }
}

/* ---- per frame ------------------------------------------------------------ */

void hc_villagers_sync(GAME_PLAY* play, HaloSim* sim, float dt) {
    int seen[HV_MAX] = { 0 };
    for (ACTOR* a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        HcVillager* v = find(a);
        if (!v) v = adopt(sim, a);
        if (v) seen[v - s_v] = 1;
    }
    for (int i = 0; i < HV_MAX; i++) {
        HcVillager* v = &s_v[i];
        if (!v->used) continue;
        if (!seen[i] || v->unit < 0 || !sim->units[v->unit].active || !sim->units[v->unit].kinematic) {
            drop(sim, v);
            continue;
        }
        HaloUnit* u = &sim->units[v->unit];
        int puppeted = v->state == HC_VILLAGER_PANICKING || v->state == HC_VILLAGER_DOWN;
        hv3 p = hc_a2h_pos(puppeted ? v->puppet : v->actor->world.position);
        /* Velocity only feeds the motion tracker. */
        u->vel = dt > 0.0f ? hv3_scale(hv3_sub(p, v->last_pos), 1.0f / dt) : hv3_make(0, 0, 0);
        if (hv3_len(u->vel) > 10.0f) u->vel = hv3_make(0, 0, 0);
        u->pos = u->prev_pos = p;
        u->yaw = u->prev_yaw = hc_a2h_yaw(v->actor->shape_info.rotation.y);
        v->last_pos = p;
        if (v->special) u->body = HV_SPECIAL_BODY;

        if (v->state == HC_VILLAGER_NORMAL || v->state == HC_VILLAGER_ALERTED) {
            for (int k = 0; k < HALO_MAX_UNITS; k++) {
                const HaloUnit* c = &sim->units[k];
                if (!c->active || c->dead || c->team != HALO_TEAM_COVENANT) continue;
                if (hv3_dist(c->pos, p) < HV_SEE_COVENANT) {
                    panic_from(v, c->pos, PICK(k_covenant));
                    break;
                }
            }
        }
    }
}

static HcVillager* by_unit(int unit) {
    if (unit < 0) return NULL;
    for (int i = 0; i < HV_MAX; i++)
        if (s_v[i].used && s_v[i].unit == unit) return &s_v[i];
    return NULL;
}

static hv3 attacker_pos(HaloSim* sim, const HaloEvent* e) {
    if (e->other >= 0 && e->other < HALO_MAX_UNITS && sim->units[e->other].active) return sim->units[e->other].pos;
    return hv3_add(e->pos, e->dir);
}

void hc_villagers_event(HaloSim* sim, const HaloEvent* e) {
    switch (e->type) {
        case HALO_EV_UNIT_DAMAGED: {
            HcVillager* v = by_unit(e->unit);
            if (!v || e->value <= 0.0f) break;
            if (v->special) {
                say(v, PICK(k_special), 0xFFE08000, 0);
                set_state(v, HC_VILLAGER_HIDING, 0.8f);
                break;
            }
            if (v->state == HC_VILLAGER_DOWN) break;
            if (v->nook) say(v, PICK(k_nook_hurt), HV_NOOK_RGB, 0);
            else say(v, PICK(k_hurt), 0xFF8A7000, 1);
            panic_from(v, attacker_pos(sim, e), NULL);
            break;
        }
        case HALO_EV_UNIT_KILLED: {
            HcVillager* v = by_unit(e->unit);
            if (!v) break;
            if (v->nook) {
                set_state(v, HC_VILLAGER_DOWN, HV_NOOK_DOWN_TIME);
                say(v, PICK(k_nook_down), HV_NOOK_RGB, 1);
                s_nook_kills++;
                s_nook_kill_pending = 1;
                s_nook_kill_pos = v->puppet;
            } else {
                set_state(v, HC_VILLAGER_DOWN, HV_DOWN_TIME);
                say(v, PICK(k_down), 0xD0D0D000, 1);
            }
            s_downed_total++;
            /* Everyone who saw it runs. */
            for (int i = 0; i < HV_MAX; i++) {
                HcVillager* w = &s_v[i];
                if (w->used && w != v && hv3_dist(w->last_pos, v->last_pos) < HV_HEAR_EXPLOSION)
                    panic_from(w, v->last_pos, PICK(k_panic));
            }
            break;
        }
        case HALO_EV_WEAPON_FIRED:
        case HALO_EV_EXPLOSION: {
            float range = e->type == HALO_EV_EXPLOSION ? HV_HEAR_EXPLOSION : HV_HEAR_GUNFIRE;
            if (e->type == HALO_EV_EXPLOSION && e->value <= 0.0f) range = 3.0f;
            for (int i = 0; i < HV_MAX; i++) {
                HcVillager* v = &s_v[i];
                if (!v->used || v->state == HC_VILLAGER_DOWN) continue;
                float d = hv3_dist(v->last_pos, e->pos);
                if (d > range) continue;
                if (v->special) {
                    if (d < range * 0.5f) say(v, PICK(k_special), 0xFFE08000, 0);
                    continue;
                }
                if (v->state == HC_VILLAGER_NORMAL && d > range * 0.6f && e->type == HALO_EV_WEAPON_FIRED) {
                    set_state(v, HC_VILLAGER_ALERTED, 0.7f);
                    v->threat = e->pos;
                    say(v, PICK(k_alert), 0xFFFFFF00, 0);
                } else if (v->state != HC_VILLAGER_PANICKING) {
                    panic_from(v, e->pos, PICK(k_panic));
                }
            }
            break;
        }
        default:
            break;
    }
}

static void drive_panic(HaloSim* sim, HcVillager* v, float dt) {
    hv3 p = hc_a2h_pos(v->puppet);
    hv3 away = hv3_sub(p, v->threat);
    away.z = 0.0f;
    float want = hv3_len_xy(away) > 0.01f ? atan2f(away.y, away.x) : v->flee_yaw;
    if (v->state_time < dt * 1.5f) v->flee_yaw = want + (hv_rand() - 0.5f) * 0.8f;
    /* Zig-zag a little, steer back toward "away". */
    v->flee_yaw += hc_wrap_angle(want - v->flee_yaw) * hc_minf(1.0f, dt * 1.5f);
    float wobble = sinf(v->state_time * 9.0f) * 0.35f;
    float yaw = v->flee_yaw + wobble;
    hv3 to = hv3_make(p.x + cosf(yaw) * HV_RUN_SPEED * dt, p.y + sinf(yaw) * HV_RUN_SPEED * dt, p.z);
    const HaloWorldApi* w = sim->world;
    if (w && w->move_biped) {
        HaloMoveResult r;
        w->move_biped(w->ctx, p, to, 0.2f, 0.7f, &r);
        if (r.in_water || (r.has_ground && fabsf(r.ground_z - p.z) > 0.3f)) {
            r.position = p; /* never run into the river or off a cliff */
            r.hit_wall = 1;
        }
        if (r.hit_wall) v->flee_yaw += (hv_rand() > 0.5f ? 1.0f : -1.0f) * HC_DEG2RAD(70.0f);
        to = r.position;
        if (r.has_ground && !r.in_water) to.z = r.ground_z;
    }
    v->puppet = hc_h2a_pos(to);
    ACTOR* a = v->actor;
    s16 ay = hc_h2a_yaw(yaw);
    a->shape_info.rotation.y = ay;
    a->world.angle.y = ay;
    a->shape_info.rotation.z = (s16)(sinf(v->state_time * 22.0f) * 0x0900);
    a->shape_info.rotation.x = (s16)(0x0600);
}

void hc_villagers_post(GAME_PLAY* play, HaloSim* sim, float dt) {
    (void)play;
    for (int i = 0; i < HV_MAX; i++) {
        HcVillager* v = &s_v[i];
        if (!v->used || !v->actor) continue;
        v->state_time += dt;
        v->line_age += dt;
        if (v->line_cooldown > 0.0f) v->line_cooldown -= dt;
        ACTOR* a = v->actor;
        switch (v->state) {
            case HC_VILLAGER_ALERTED:
                if (v->state_time > v->state_len) panic_from(v, v->threat, PICK(k_panic));
                break;
            case HC_VILLAGER_PANICKING:
                drive_panic(sim, v, dt);
                if (v->state_time > v->state_len) {
                    set_state(v, HC_VILLAGER_HIDING, HV_HIDE_TIME);
                    say(v, PICK(k_hide), 0xB0D8FF00, 1);
                }
                break;
            case HC_VILLAGER_HIDING:
                /* Shake in place, squashed down. */
                a->shape_info.rotation.z = (s16)(sinf(v->state_time * 40.0f) * 0x0300);
                a->shape_info.rotation.x = (s16)0x0C00;
                if (v->state_time > v->state_len) set_state(v, HC_VILLAGER_NORMAL, 0.0f);
                break;
            case HC_VILLAGER_DOWN: {
                float k = hc_clampf(v->state_time / 0.35f, 0.0f, 1.0f);
                a->shape_info.rotation.x = (s16)(-0x3C00 * k);
                a->shape_info.rotation.z = 0;
                /* Shop scripts keep turning to face the customer. */
                a->shape_info.rotation.y = a->world.angle.y = v->down_yaw;
                if (v->state_time > v->state_len) {
                    halo_revive_unit(sim, v->unit);
                    set_state(v, HC_VILLAGER_NORMAL, 0.0f);
                    say(v, PICK(k_getup), 0xA0FFA000, 1);
                }
                break;
            }
            default:
                break;
        }
        if (v->state == HC_VILLAGER_PANICKING || v->state == HC_VILLAGER_DOWN) {
            a->world.position = v->puppet;
            a->position_speed.x = a->position_speed.z = 0.0f;
            a->speed = 0.0f;
        }
    }
}

void hc_villagers_labels(HcHudText* hud) {
    for (int i = 0; i < HV_MAX && hud->label_count < HC_MAX_LABELS; i++) {
        HcVillager* v = &s_v[i];
        if (!v->used || !v->actor || !v->line[0] || v->line_age > HV_LINE_TIME) continue;
        HcWorldLabel* l = &hud->labels[hud->label_count++];
        l->pos = v->actor->world.position;
        l->pos.y += v->state == HC_VILLAGER_DOWN ? HV_HEAD_AC * 0.45f : HV_HEAD_AC;
        snprintf(l->text, sizeof(l->text), "%s", v->line);
        l->alpha = hc_clampf((HV_LINE_TIME - v->line_age) / 0.4f, 0.0f, 1.0f);
        l->rgb = v->line_rgb;
    }
}

int hc_villagers_count_state(HcVillagerState s) {
    int n = 0;
    for (int i = 0; i < HV_MAX; i++)
        if (s_v[i].used && s_v[i].state == s) n++;
    return n;
}

int hc_villagers_downed_total(void) {
    return s_downed_total;
}

int hc_villagers_is_nook(int unit) {
    HcVillager* v = by_unit(unit);
    return v != NULL && v->nook;
}

int hc_villagers_nook_kills(void) {
    return s_nook_kills;
}

int hc_villagers_take_nook_kill(xyz_t* pos) {
    if (!s_nook_kill_pending) return 0;
    s_nook_kill_pending = 0;
    if (pos) *pos = s_nook_kill_pos;
    return 1;
}

const char* hc_villagers_summary(void) {
    int n = 0, special = 0;
    for (int i = 0; i < HV_MAX; i++) {
        if (!s_v[i].used) continue;
        n++;
        special += s_v[i].special;
    }
    snprintf(s_summary, sizeof(s_summary), "  npc %d/%d sp  run %d hide %d down %d (%d total)", n, special,
             hc_villagers_count_state(HC_VILLAGER_PANICKING), hc_villagers_count_state(HC_VILLAGER_HIDING),
             hc_villagers_count_state(HC_VILLAGER_DOWN), s_downed_total);
    return s_summary;
}
