/* Milestone 1 prototype: Halo combat running inside the Animal Crossing town.
 * Owns the host hooks, the camera mode toggle, the player <-> villager sync,
 * debug keys and the debug overlay. */
#include "hc_hooks.h"

#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dolphin/pad.h"
#include "graph.h"
#include "halo/halo_sim.h"
#include "hc_ac_world.h"
#include "hc_draw.h"
#include "hc_fp_view.h"
#include "hc_gfx.h"
#include "hc_input.h"
#include "hc_invasion.h"
#include "hc_villagers.h"
#include "hc_world_scale.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_msg.h"
#include "m_name_table.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_random_field_h.h"
#include "m_scene.h"
#include "m_scene_table.h"
#include "m_view.h"

extern int g_pc_paused;

#define HC_FOV_Y 55.0f     /* Halo CE's 70 degree horizontal FOV at 4:3 */
#define HC_NEAR 0.25f      /* the imported first-person arms come within 0.35 AC units of the eye */
#define HC_FAR 3200.0f
#define HC_SPAWN_AHEAD 6.0f
#define HC_LOG_LINES 3
#define HC_LOG_LIFE 6.0f
#define HC_AI_WAKE_RANGE 20.0f   /* wu: idle squads farther than this sleep */
#define HC_INVASION_CLEARANCE 10.0f /* wu kept free of Covenant around the player */

typedef struct HcState {
    int inited;
    int active;
    int first_person;      /* F1 */
    int fps_live;          /* first person and not handed back to AC this frame */
    int show_overlay;
    HaloSim sim;
    HcInput input;
    HcView view;
    HcHudText hud;
    float time;
    float fps;
    float msg_timer;
    float bob_phase;
    int autospawn_pending;
    int no_capture;        /* HC_NO_CAPTURE: unattended runs never grab the mouse */
    FILE* event_log;       /* HC_EVENT_LOG: every log line, for scripted checks */
    unsigned updates;      /* play updates since the last HUD draw */
    unsigned stalled_draws;
    char log[HC_LOG_LINES][80]; /* newest first */
    float log_age[HC_LOG_LINES];
    int kills;
    int zoom_level;        /* 0 = unscoped, else index into the weapon's magnifications + 1 */
    int invasion_enabled;  /* HC_INVASION=0 turns the town invasion off */
    int invasion_pending;  /* populate once the Halo camera goes live in town */
    int attract;           /* HC_ATTRACT=1: plays itself (weapon tour) when the mouse isn't captured */
    int invincible;        /* F8; on unless HC_INVINCIBLE=0. Outlives the per-scene sim reset. */
    int attract_weapon;
    int attract_threw;
    int attract_frame;
    int shops_never_close; /* on unless HC_SHOP_HOURS=1 */
    int warp_shop;         /* HC_WARP=shop: walk into Nook's shop once the town is up (unattended checks) */
    int warp_phase;        /* 0 waiting, 1 loading the shop's block, 2 walking to the door, 3 done */
    float warp_timer;
    int warp_door;
    float scene_time;
} HcState;

static HcState g;

static void hc_message(const char* s) {
    snprintf(g.hud.center, sizeof(g.hud.center), "%s", s);
    g.msg_timer = 2.0f;
}

static void hc_log(const char* fmt, ...) {
    for (int i = HC_LOG_LINES - 1; i > 0; i--) {
        memcpy(g.log[i], g.log[i - 1], sizeof(g.log[i]));
        g.log_age[i] = g.log_age[i - 1];
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.log[0], sizeof(g.log[0]), fmt, ap);
    va_end(ap);
    g.log_age[0] = 0.0f;
    if (g.event_log) {
        fprintf(g.event_log, "%9.2f %s\n", g.sim.time, g.log[0]);
        fflush(g.event_log);
    }
}

static const char* unit_name(int ui) {
    static const char* names[HALO_BIPED_COUNT] = { "chief", "grunt", "elite", "villager" };
    if (ui < 0 || ui >= HALO_MAX_UNITS) return "world";
    return names[g.sim.units[ui].biped];
}

static float zoom_magnification(const HaloUnit* p) {
    if (!p || p->dead || g.zoom_level <= 0 || p->weapon.id == HALO_WEAPON_NONE) return 1.0f;
    const HaloWeaponDef* wd = &g_halo_weapons[p->weapon.id];
    if (g.zoom_level > wd->zoom_levels) return 1.0f;
    return wd->zoom_magnification[g.zoom_level - 1];
}

static void update_zoom(HaloUnit* p) {
    if (hc_input_take_zoom(&g.input) && p && !p->dead && p->weapon.id != HALO_WEAPON_NONE) {
        int levels = g_halo_weapons[p->weapon.id].zoom_levels;
        g.zoom_level = levels > 0 ? (g.zoom_level + 1) % (levels + 1) : 0;
    }
    /* CE drops out of the scope to reload, on death and on a switch. */
    if (!p || p->dead || p->weapon.reload_timer > 0.0f || p->weapon.ready_timer > 0.0f) g.zoom_level = 0;
    float mag = zoom_magnification(p);
    g.view.zoom = mag;
    g.input.sens_scale = 1.0f / mag;
}

static float view_fov_y(void) {
    if (g.view.zoom <= 1.01f) return HC_FOV_Y;
    return HC_RAD2DEG(2.0f * atanf(tanf(HC_DEG2RAD(HC_FOV_Y) * 0.5f) / g.view.zoom));
}

static void log_event(const HaloEvent* e) {
    int player = g.sim.player;
    switch (e->type) {
        case HALO_EV_UNIT_DAMAGED:
            if (e->value <= 0.0f) break;
            if (e->unit < 0 || (e->other != player && e->unit != player)) break;
            hc_log("%s hit %s %.1f  sh %.0f hp %.0f", unit_name(e->other), unit_name(e->unit), e->value,
                   g.sim.units[e->unit].shield, g.sim.units[e->unit].body);
            break;
        case HALO_EV_UNIT_KILLED:
            if (hc_villagers_is_nook(e->unit)) {
                hc_log("tom nook killed by %s", unit_name(e->other));
                break;
            }
            if (e->unit >= 0 && g.sim.units[e->unit].biped == HALO_BIPED_VILLAGER) {
                hc_log("villager knocked out by %s", unit_name(e->other));
                break;
            }
            if (e->unit != player) g.kills++;
            hc_log("%s killed by %s", unit_name(e->unit), unit_name(e->other));
            break;
        case HALO_EV_AI_ALERTED: {
            /* Squads re-alert on every shot they hear; only log the first. */
            static float last_alert[HALO_MAX_UNITS];
            if (e->unit >= 0 && e->unit < HALO_MAX_UNITS && g.time - last_alert[e->unit] > 8.0f) {
                last_alert[e->unit] = g.time;
                hc_log("%s alerted", unit_name(e->unit));
            }
            break;
        }
        case HALO_EV_AI_PANIC:
            hc_log("%s panics", unit_name(e->unit));
            break;
        case HALO_EV_WEAPON_SWITCHED:
            if (e->unit == player) {
                g.view.weapon_switch_age = 0.0f;
                g.zoom_level = 0;
                hc_log("chief draws the %s", g_halo_weapons[e->def].hud_name);
            }
            break;
        case HALO_EV_PROJECTILE_IMPACT: {
            static float last;
            HaloUnit* p = halo_player(&g.sim);
            if (p && e->unit == player && e->other < 0 && g.time - last > 0.5f) {
                last = g.time;
                hc_log("chief shot hit world at %.1f wu", hv3_dist(e->pos, halo_unit_eye(p)));
            }
            break;
        }
        default:
            break;
    }
}

static void hc_init_once(void) {
    if (g.inited) return;
    g.inited = 1;
    hc_input_init(&g.input);
    g.show_overlay = 1;
    const char* fp = getenv("HC_FIRST_PERSON");
    g.first_person = fp && fp[0] == '1';
    const char* sp = getenv("HC_AUTOSPAWN");
    g.autospawn_pending = sp && sp[0] == '1';
    const char* nc = getenv("HC_NO_CAPTURE");
    g.no_capture = nc && nc[0] == '1';
    const char* el = getenv("HC_EVENT_LOG");
    if (el && el[0]) g.event_log = fopen(el, "a");
    const char* inv = getenv("HC_INVASION");
    g.invasion_enabled = !(inv && inv[0] == '0');
    const char* at = getenv("HC_ATTRACT");
    g.attract = at && at[0] == '1';
    const char* va = getenv("HC_VILLAGER_ALL");
    hc_villagers_set_treat_all(va && va[0] == '1');
    const char* gm = getenv("HC_INVINCIBLE");
    g.invincible = !(gm && gm[0] == '0');
    const char* sh = getenv("HC_SHOP_HOURS");
    g.shops_never_close = !(sh && sh[0] == '1');
    const char* wp = getenv("HC_WARP");
    g.warp_shop = wp && strcmp(wp, "shop") == 0;
    hc_fp_view_init();
}

/* Attract mode: a 3 s slot per weapon, aiming at the nearest living
 * non-player unit and pulling the trigger, so unattended runs exercise
 * the whole arsenal. */
static void attract(HaloUnit* p) {
    const float slot = 3.0f;
    int w = (int)(g.time / slot) % HALO_WEAPON_COUNT;
    float phase = fmodf(g.time, slot);
    g.attract_frame++;
    if (w != g.attract_weapon) {
        g.attract_weapon = w;
        g.attract_threw = 0;
    }
    /* The arsenal refuses to switch mid-throw, so keep asking. */
    if ((int)p->weapon.id != w) p->control.weapon_select = w + 1;
    if (w == 0 && phase > 1.5f && !g.attract_threw) {
        g.attract_threw = 1;
        p->control.grenade_pressed = 1;
    }
    const HaloWorldApi* world = g.sim.world;
    hv3 eye = halo_unit_eye(p);
    int best = -1, covenant_seen = 0;
    float best_d = 25.0f;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &g.sim.units[i];
        if (!u->active || u->dead || u->is_player) continue;
        float d = hv3_dist(u->pos, p->pos);
        if (d > 25.0f) continue;
        hv3 c = hv3_make(u->pos.x, u->pos.y, u->pos.z + halo_unit_height(u) * 0.6f);
        HaloRayHit hit;
        if (world && world->raycast && world->raycast(world->ctx, eye, c, &hit)) continue;
        if (u->team == HALO_TEAM_COVENANT) {
            covenant_seen = 1;
            d -= 3.0f;
        }
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    /* Keep the tour supplied with something to shoot, somewhere it can be seen. */
    if (!covenant_seen && g.attract_frame % 120 == 0) {
        HaloActorTypeId type = (g.attract_frame / 120) % 2 ? HALO_ACTOR_ELITE : HALO_ACTOR_GRUNT;
        for (int k = 0; k < 16; k++) {
            float yaw = p->control.aim_yaw + (float)((k + 1) / 2) * (k % 2 ? 0.4f : -0.4f);
            float dist = 4.0f + (float)(k % 3);
            hv3 at = hv3_make(p->pos.x + cosf(yaw) * dist, p->pos.y + sinf(yaw) * dist, p->pos.z);
            if (!hc_ac_world_spawn_ok(&at)) continue;
            HaloRayHit hit;
            hv3 c = hv3_make(at.x, at.y, at.z + 0.8f);
            if (world && world->raycast && world->raycast(world->ctx, eye, c, &hit)) continue;
            int i = halo_spawn_actor(&g.sim, type, at, hc_wrap_angle(yaw + HC_PI));
            if (i >= 0) g.sim.units[i].grounded = 1;
            break;
        }
    }
    if (best < 0) p->control.aim_pitch *= 0.9f;
    if (best >= 0) {
        const HaloUnit* t = &g.sim.units[best];
        hv3 c = hv3_make(t->pos.x, t->pos.y, t->pos.z + halo_unit_height(t) * 0.6f);
        hv3 d = hv3_sub(c, halo_unit_eye(p));
        p->control.aim_yaw = atan2f(d.y, d.x);
        p->control.aim_pitch = atan2f(d.z, hv3_len_xy(d));
    }
    int automatic = g_halo_weapons[w].trigger.automatic;
    int hold = phase > 1.0f && phase < 2.6f && best >= 0;
    p->control.fire = hold && (automatic || (g.attract_frame / 6) % 2 == 0);
    if (w == HALO_WEAPON_SNIPER_RIFLE && p->weapon.id == w && p->weapon.ready_timer <= 0.0f &&
        g.zoom_level < (phase > 2.0f ? 2 : 1))
        g.input.zoom = 1;
}

static int town_scene(GAME_PLAY* play) {
    return play->scene_id == SCENE_FG || play->scene_id == SCENE_TITLE_DEMO;
}

static void invade(GAME_PLAY* play, int clear_first) {
    HaloUnit* p = halo_player(&g.sim);
    if (!p || !town_scene(play)) return;
    int removed = clear_first ? hc_invasion_clear(&g.sim) : 0;
    int n = hc_invasion_populate(&g.sim, p->pos, HC_INVASION_CLEARANCE, HC_INVASION_SQUADS);
    char buf[48];
    snprintf(buf, sizeof(buf), "COVENANT INVASION: %d", halo_count_living(&g.sim, HALO_TEAM_COVENANT));
    hc_message(buf);
    hc_log("invasion: %d spawned, %d cleared", n, removed);
}

static int dialogue_open(void) {
    mMsg_Window_c* w = mMsg_Get_base_window_p();
    return w != NULL && !mMsg_Check_MainHide(w);
}

/* Walking through a door, arriving through one, cutscenes: AC animates the
 * player itself and the Halo layer hands control over until it's done. */
static int ac_owns_player(const PLAYER_ACTOR* pl) {
    switch (pl->now_main_index) {
        case mPlayer_INDEX_DMA:
        case mPlayer_INDEX_INTRO:
        case mPlayer_INDEX_RETURN_DEMO:
        case mPlayer_INDEX_RETURN_OUTDOOR:
        case mPlayer_INDEX_RETURN_OUTDOOR2:
        case mPlayer_INDEX_DOOR:
        case mPlayer_INDEX_OUTDOOR:
        case mPlayer_INDEX_INVADE:
        case mPlayer_INDEX_KNOCK_DOOR:
            return 1;
        default:
            return 0;
    }
}

static hv3 ground_at(hv3 p) {
    const HaloWorldApi* w = hc_ac_world_api();
    float z;
    if (w->ground_height(w->ctx, hv3_make(p.x, p.y, p.z + 1.0f), &z)) p.z = z;
    return p;
}

static HaloUnit* ensure_player(PLAYER_ACTOR* pl) {
    HaloUnit* p = halo_player(&g.sim);
    if (p) return p;
    hv3 pos = hc_a2h_pos(pl->actor_class.world.position);
    float yaw = hc_a2h_yaw(pl->actor_class.shape_info.rotation.y);
    int i = halo_spawn_player(&g.sim, pos, yaw);
    if (i < 0) return NULL;
    p = &g.sim.units[i];
    p->grounded = 1;
    p->control.aim_yaw = yaw;
    return p;
}

static int spawn_ahead(HaloActorTypeId type, float dist, float side) {
    HaloUnit* p = halo_player(&g.sim);
    if (!p) return -1;
    float yaw = p->control.aim_yaw;
    hv3 at = hv3_make(p->pos.x + cosf(yaw) * dist - sinf(yaw) * side, p->pos.y + sinf(yaw) * dist + cosf(yaw) * side,
                      p->pos.z);
    at = ground_at(at);
    int i = halo_spawn_actor(&g.sim, type, at, hc_wrap_angle(yaw + HC_PI));
    if (i >= 0) g.sim.units[i].grounded = 1;
    return i;
}

static void sync_halo_from_ac(PLAYER_ACTOR* pl, HaloUnit* p) {
    hv3 np = hc_a2h_pos(pl->actor_class.world.position);
    p->pos = p->prev_pos = np;
    p->vel = hv3_make(0, 0, 0);
    p->grounded = 1;
    p->yaw = p->prev_yaw = hc_a2h_yaw(pl->actor_class.shape_info.rotation.y);
    p->control.aim_yaw = p->yaw;
    p->control.aim_pitch = 0.0f;
    p->control.throttle_forward = p->control.throttle_left = 0.0f;
    p->control.fire = 0;
}

static void sync_ac_from_halo(PLAYER_ACTOR* pl, const HaloUnit* p) {
    ACTOR* a = &pl->actor_class;
    xyz_t ap = hc_h2a_pos(halo_unit_pos_interp(p, g.sim.alpha));
    a->world.position = ap;
    a->position_speed.x = a->position_speed.y = a->position_speed.z = 0.0f;
    s16 yaw = hc_h2a_yaw(p->yaw);
    a->shape_info.rotation.y = yaw;
    /* Shop doors only open for a player pushing toward them, and a Chief
     * pressed against the door has no velocity left; go by the stick. */
    float tf = p->control.throttle_forward, tl = p->control.throttle_left;
    float push = sqrtf(tf * tf + tl * tl);
    float speed = hv3_len_xy(p->vel);
    a->speed = push > 0.2f ? hc_clampf(hc_h2a_len(speed) / 30.0f, 1.0f, 7.5f) : 0.0f;
    a->world.angle.y = a->speed > 0.0f ? hc_h2a_yaw(p->control.aim_yaw + atan2f(tl, tf)) : yaw;
}

/* ---- HC_WARP=shop ---------------------------------------------------------- */

typedef struct HcShopDoor {
    s16 profile;
    float dx, dz;       /* the door point each building's check_player measures from */
    Door_data_c enter;
} HcShopDoor;

static const HcShopDoor k_shop_doors[] = {
    { mAc_PROFILE_SHOP, -38.0f, 42.0f, { SCENE_SHOP0, mSc_DIRECT_NORTH, FALSE, 0, { 160, 0, 300 }, EMPTY_NO, 1, { 0 } } },
    { mAc_PROFILE_CONVENI, -14.0f, 76.0f, { SCENE_CONVENI, mSc_DIRECT_NORTH, FALSE, 0, { 320, 0, 300 }, EMPTY_NO, 1, { 0 } } },
    { mAc_PROFILE_SUPER, -38.0f, 96.0f, { SCENE_SUPER, mSc_DIRECT_NORTH, FALSE, 0, { 320, 0, 460 }, EMPTY_NO, 1, { 0 } } },
    { mAc_PROFILE_DEPART, -38.0f, 96.0f, { SCENE_DEPART, mSc_DIRECT_NORTH, FALSE, 0, { 320, 0, 540 }, EMPTY_NO, 1, { 0 } } },
};
#define HC_SHOP_DOOR_FACING 0x6000 /* AC yaw 135 degrees: the doors face south-west */

static void teleport(HaloUnit* p, xyz_t at, s16 ac_yaw) {
    hv3 h = hc_a2h_pos(at);
    h.z = 30.0f;
    p->pos = p->prev_pos = ground_at(h);
    p->vel = hv3_make(0, 0, 0);
    p->control.aim_yaw = p->yaw = p->prev_yaw = hc_a2h_yaw(ac_yaw);
    p->control.aim_pitch = 0.0f;
}

static ACTOR* find_shop_building(GAME_PLAY* play, int* door) {
    for (ACTOR* a = play->actor_info.list[ACTOR_PART_ITEM].actor; a != NULL; a = a->next_actor) {
        for (int k = 0; k < (int)(sizeof(k_shop_doors) / sizeof(k_shop_doors[0])); k++) {
            if (a->id != k_shop_doors[k].profile) continue;
            *door = k;
            return a;
        }
    }
    return NULL;
}

/* Puts the Chief in front of the shop, waits for its block to load, then
 * walks him at the door from the south-west so unattended runs go through
 * the real door code. Warps straight in if that fails. */
static int warp_to_shop(GAME_PLAY* play, HaloUnit* p, float dt) {
    p->control.throttle_forward = p->control.throttle_left = 0.0f;
    if (g.warp_phase == 0) {
        if (!town_scene(play) || g.scene_time < 3.0f) return 0;
        /* The block kind table isn't filled in for the title demo; look for
         * the building itself in every block's field items. */
        int bx = -1, bz = -1, ux = 8, uz = 8;
        for (int z = 0; z < BLOCK_Z_NUM && bx < 0; z++) {
            for (int x = 0; x < BLOCK_X_NUM && bx < 0; x++) {
                for (int k = 0; k < 4; k++) {
                    if (mFI_SearchFGInBlock(&ux, &uz, (mActor_name_t)(SHOP0 + k), x, z)) {
                        bx = x;
                        bz = z;
                        break;
                    }
                }
            }
        }
        if (bx < 0 && !mFI_BlockKind2BkNum(&bx, &bz, mRF_BLOCKKIND_SHOP)) {
            g.warp_phase = 3;
            hc_log("warp: no shop in this town");
            return 0;
        }
        xyz_t at;
        mFI_BkandUtNum2CenterWpos(&at, bx, bz, ux, uz);
        at.z += 120.0f;
        teleport(p, at, HC_SHOP_DOOR_FACING);
        g.warp_phase = 1;
        g.warp_timer = 0.0f;
        hc_log("warp: shop block %d,%d unit %d,%d", bx, bz, ux, uz);
        return 1;
    }
    if (g.warp_phase == 1) {
        g.warp_timer += dt;
        ACTOR* shop = find_shop_building(play, &g.warp_door);
        if (shop) {
            xyz_t at = shop->world.position;
            at.x += k_shop_doors[g.warp_door].dx - 30.0f;
            at.z += k_shop_doors[g.warp_door].dz + 30.0f;
            teleport(p, at, HC_SHOP_DOOR_FACING);
            g.warp_phase = 2;
            g.warp_timer = 0.0f;
            hc_log("warp: walking into the shop door");
        } else if (g.warp_timer > 4.0f) {
            g.warp_phase = 3;
            hc_log("warp: the shop building never loaded");
        }
        return 1;
    }
    if (g.warp_phase != 2) return 0;
    g.warp_timer += dt;
    p->control.throttle_forward = 1.0f;
    p->control.aim_yaw = hc_a2h_yaw(HC_SHOP_DOOR_FACING);
    if (g.warp_timer > 6.0f) {
        g.warp_phase = 3;
        Door_data_c* out = Common_GetPointer(structure_exit_door_data);
        xyz_t ap = hc_h2a_pos(p->pos);
        out->next_scene_id = Save_Get(scene_no);
        out->exit_orientation = mSc_DIRECT_SOUTH_WEST;
        out->exit_type = 0;
        out->extra_data = 3;
        out->exit_position.x = (s16)ap.x;
        out->exit_position.y = (s16)ap.y;
        out->exit_position.z = (s16)ap.z;
        out->door_actor_name = EMPTY_NO;
        out->wipe_type = WIPE_TYPE_FADE_BLACK;
        Door_data_c enter = k_shop_doors[g.warp_door].enter;
        int ok = goto_other_scene(play, &enter, FALSE);
        hc_log("warp: the door never opened, warping (%d)", ok);
    }
    return 1;
}

int hc_hook_shops_never_close(void) {
    hc_init_once();
    return g.shops_never_close;
}

static void handle_fkeys(GAME_PLAY* play) {
    HcInput* in = &g.input;
    if (hc_input_take_fkey(in, 1)) {
        g.first_person = !g.first_person;
        HaloUnit* p = halo_player(&g.sim);
        if (p && g.first_person) {
            g.sim.player_spawn = p->pos;
            g.sim.player_spawn_yaw = p->yaw;
        }
        hc_message(g.first_person ? "HALO CAMERA" : "ANIMAL CROSSING CAMERA");
    }
    if (hc_input_take_fkey(in, 2)) g.view.show_collision = !g.view.show_collision;
    if (hc_input_take_fkey(in, 3)) g.view.show_ai = !g.view.show_ai;
    if (hc_input_take_fkey(in, 4)) g.view.show_nav = !g.view.show_nav;
    if (hc_input_take_fkey(in, 5)) {
        if (spawn_ahead(HALO_ACTOR_GRUNT, HC_SPAWN_AHEAD, 0.0f) >= 0) hc_message("GRUNT SPAWNED");
    }
    if (hc_input_take_fkey(in, 6)) {
        if (spawn_ahead(HALO_ACTOR_ELITE, HC_SPAWN_AHEAD + 1.0f, 0.0f) >= 0) hc_message("ELITE SPAWNED");
    }
    if (hc_input_take_fkey(in, 7)) {
        HaloUnit* p = halo_player(&g.sim);
        if (p && !p->dead) {
            halo_give_arsenal(&g.sim);
            g.view.weapon_switch_age = 0.0f;
            hc_message("FULL ARSENAL");
        }
    }
    if (hc_input_take_fkey(in, 8)) {
        g.invincible = !g.invincible;
        hc_message(g.invincible ? "INVINCIBLE ON" : "INVINCIBLE OFF");
    }
    if (hc_input_take_fkey(in, 9)) {
        int n = 0;
        for (int i = 0; i < HALO_MAX_UNITS; i++) {
            HaloUnit* u = &g.sim.units[i];
            if (u->active && !u->dead && u->team == HALO_TEAM_COVENANT) {
                halo_kill_unit(&g.sim, i, g.sim.player);
                n++;
            }
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "KILLED %d", n);
        hc_message(buf);
    }
    if (hc_input_take_fkey(in, 10)) g.show_overlay = !g.show_overlay;
    if (hc_input_take_fkey(in, 11)) {
        if (town_scene(play)) invade(play, 1);
        else hc_message("NO INVASIONS INDOORS");
    }
}

static void build_overlay(GAME_PLAY* play, PLAYER_ACTOR* pl) {
    HcHudText* t = &g.hud;
    t->count = 0;
    if (!g.show_overlay) return;
    HaloUnit* p = halo_player(&g.sim);
    xyz_t ap = pl->actor_class.world.position;
    HcGfxStats* gs = hc_gfx_stats();
    HcAcWorldStats* ws = hc_ac_world_stats();

    snprintf(t->lines[t->count++], sizeof(t->lines[0]), "FPS %3.0f  tick %u  %s  scene %d  main %d  head %.1f%s",
             g.fps, g.sim.tick, g.fps_live ? "HALO" : "AC", play->scene_id, pl->now_main_index,
             pl->head_pos.y - ap.y, dialogue_open() ? "  DLG" : "");
    if (p) {
        snprintf(t->lines[t->count++], sizeof(t->lines[0]), "chief (%.2f %.2f %.2f)wu  ac (%.0f %.0f %.0f)", p->pos.x,
                 p->pos.y, p->pos.z, ap.x, ap.y, ap.z);
        const char* wn = p->weapon.id != HALO_WEAPON_NONE ? g_halo_weapons[p->weapon.id].hud_name : "NONE";
        snprintf(t->lines[t->count++], sizeof(t->lines[0]), "%s %d/%d  sh %.0f  hp %.0f  frag %d plasma %d%s", wn,
                 p->weapon.rounds_loaded, p->weapon.rounds_reserve, p->shield, p->body,
                 p->grenades[HALO_GRENADE_FRAG], p->grenades[HALO_GRENADE_PLASMA],
                 g.sim.infinite_shields ? "  [INVINCIBLE]" : "");
    }
    int by_state[HALO_AI_STATE_COUNT] = { 0 };
    int n = 0;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        if (!g.sim.ai[i].active || !g.sim.units[i].active || g.sim.units[i].dead) continue;
        n++;
        by_state[g.sim.ai[i].state]++;
    }
    snprintf(t->lines[t->count++], sizeof(t->lines[0]),
             "covenant %d (idle %d asleep %d alert %d combat %d search %d flee %d)  kills %d%s", n,
             by_state[HALO_AI_IDLE], g.sim.dormant_count, by_state[HALO_AI_ALERT], by_state[HALO_AI_COMBAT],
             by_state[HALO_AI_SEARCH], by_state[HALO_AI_FLEE], g.kills, hc_villagers_summary());
    snprintf(t->lines[t->count++], sizeof(t->lines[0]), "gfx %d/%d/%d ovf %d  rays %d lc %d path %d  vm %s",
             gs->opa_used, gs->xlu_used, gs->font_used, gs->overflows, ws->rays, ws->line_checks, ws->paths,
             p && hc_fp_view_available(p->weapon.id) ? "halo" : "boxes");
    for (int i = 0; i < HC_LOG_LINES && t->count < HC_DEBUG_LINES; i++) {
        if (g.log[i][0] && g.log_age[i] < HC_LOG_LIFE) snprintf(t->lines[t->count++], sizeof(t->lines[0]), "> %s", g.log[i]);
    }
}

void hc_hook_play_init(GAME_PLAY* play) {
    hc_init_once();
    halo_sim_init(&g.sim, hc_ac_world_api(), (unsigned int)SDL_GetTicks() | 1u);
    g.sim.arsenal_enabled = 1;
    g.sim.ai_activation_range = HC_AI_WAKE_RANGE;
    hc_ac_world_invalidate();
    hc_fx_clear();
    hc_villagers_reset();
    g.invasion_pending = g.invasion_enabled;
    g.zoom_level = 0;
    g.active = 0;
    g.scene_time = 0.0f;
    if (g.warp_phase == 1 || g.warp_phase == 2) {
        g.warp_phase = 3;
        hc_log("warp: through the door into scene %d", play->scene_id);
    }
}

void hc_hook_play_cleanup(GAME_PLAY* play) {
    (void)play;
    hc_input_set_capture(&g.input, 0);
    g.active = 0;
    g.fps_live = 0;
}

void hc_hook_play_update(GAME_PLAY* play) {
    hc_init_once();
    g.updates++;
    float dt = (float)play->game.graph->dt;
    if (!(dt > 0.0f) || dt > 0.25f) dt = 1.0f / 60.0f;
    g.time += dt;
    g.scene_time += dt;
    g.fps += (1.0f / dt - g.fps) * 0.05f;
    g.view.time = g.time;

    PLAYER_ACTOR* pl = GET_PLAYER_ACTOR(play);
    g.active = pl != NULL;
    if (!g.active) {
        g.fps_live = 0;
        hc_input_set_capture(&g.input, 0);
        return;
    }

    handle_fkeys(play);
    g.fps_live = g.first_person && !dialogue_open() && !g_pc_paused && !ac_owns_player(pl);
    hc_input_set_capture(&g.input, g.fps_live && !g.no_capture && SDL_GetKeyboardFocus() != NULL);

    HaloUnit* p = ensure_player(pl);
    if (p == NULL) return;
    int touring = g.attract && !g.input.capture;
    g.sim.infinite_shields = g.invincible || touring;
    if (g.fps_live && !p->dead) {
        hc_input_apply(&g.input, &p->control);
        int warping = g.warp_shop && warp_to_shop(play, p, dt);
        if (touring && !warping) attract(p);
    } else if (!g.fps_live) {
        sync_halo_from_ac(pl, p);
    }

    if (g.autospawn_pending && g.fps_live) {
        g.autospawn_pending = 0;
        spawn_ahead(HALO_ACTOR_GRUNT, HC_SPAWN_AHEAD, 0.0f);
    }
    if (g.invasion_pending && g.fps_live && town_scene(play)) {
        g.invasion_pending = 0;
        invade(play, 0);
    }

    HcAcWorldStats* ws = hc_ac_world_stats();
    memset(ws, 0, sizeof(*ws));
    hc_villagers_sync(play, &g.sim, dt);
    halo_sim_advance(&g.sim, dt);

    p = halo_player(&g.sim);
    if (g.fps_live && p) sync_ac_from_halo(pl, p);
    pl->actor_class.skip_drawing = (u8)(g.fps_live ? TRUE : FALSE);

    for (int i = 0; i < g.sim.event_count; i++) {
        hc_fx_from_event(&g.sim, &g.sim.events[i]);
        hc_fp_view_event(&g.sim, &g.sim.events[i]);
        hc_villagers_event(&g.sim, &g.sim.events[i]);
        log_event(&g.sim.events[i]);
    }
    halo_sim_clear_events(&g.sim);
    hc_villagers_post(play, &g.sim, dt);
    xyz_t nook_at;
    if (hc_villagers_take_nook_kill(&nook_at)) {
        g.kills++;
        hc_fx_bells(nook_at);
        hc_message(hc_villagers_nook_kills() > 1 ? "TOM NOOK KILLED. AGAIN." : "TOM NOOK KILLED");
    }
    for (int i = 0; i < HC_LOG_LINES; i++) g.log_age[i] += dt;
    hc_fx_update(dt);
    hc_fp_view_update(p, dt);
    update_zoom(p);
    g.view.weapon_switch_age += dt;
    g.hud.label_count = 0;
    hc_villagers_labels(&g.hud);

    if (p) {
        float speed = hv3_len_xy(p->vel);
        g.bob_phase += speed * dt * 5.5f;
        g.view.bob_phase = g.bob_phase;
        g.view.bob_amount = p->grounded ? hc_clampf(speed / 2.25f, 0.0f, 1.0f) : 0.0f;
    }
    if (g.msg_timer > 0.0f) g.msg_timer -= dt;
    g.hud.center_alpha = hc_clampf(g.msg_timer, 0.0f, 1.0f);
    build_overlay(play, pl);
}

void hc_hook_camera(GAME_PLAY* play) {
    if (!g.active || !g.fps_live) {
        g.view.first_person = 0;
        return;
    }
    HaloUnit* p = halo_player(&g.sim);
    if (!p) return;
    hv3 eye = halo_unit_eye_interp(p, g.sim.alpha);
    float yaw = p->control.aim_yaw, pitch = p->control.aim_pitch;
    if (p->dead) {
        /* Drop to the ground and look up, like CE's death cam. */
        float k = hc_clampf(p->dead_time / 0.6f, 0.0f, 1.0f);
        eye.z = p->pos.z + 0.62f - 0.5f * k;
        pitch = pitch + (HC_DEG2RAD(40.0f) - pitch) * k;
    }
    xyz_t e = hc_h2a_pos(eye);
    xyz_t f = hc_h2a_dir(hv3_from_angles(yaw, pitch));
    xyz_t c = { e.x + f.x * 100.0f, e.y + f.y * 100.0f, e.z + f.z * 100.0f };
    xyz_t up = { 0.0f, 1.0f, 0.0f };
    float fov = view_fov_y();
    setPerspectiveView(&play->view, fov, HC_NEAR, HC_FAR);
    setLookAtView(&play->view, &e, &c, &up);

    g.view.first_person = 1;
    g.view.eye = e;
    g.view.yaw = yaw;
    g.view.pitch = pitch;
    hc_draw_sky(play, &g.view, fov);
}

void hc_hook_draw_world(GAME_PLAY* play) {
    if (!g.active) return;
    if (!g.fps_live) {
        g.view.first_person = 0;
        g.view.eye = play->view.eye;
    }
    hc_draw_world(play, &g.sim, &g.view);
}

void hc_hook_draw_hud(GAME_PLAY* play) {
    /* Actor updates only run outside fbdemo transitions with no submenu open;
     * say which gate is closed when the host stops calling the update hook. */
    g.stalled_draws = g.updates ? 0 : g.stalled_draws + 1;
    g.updates = 0;
    if ((g.stalled_draws > 30 || !g.active) && g.show_overlay) {
        HcHudText t;
        memset(&t, 0, sizeof(t));
        snprintf(t.lines[0], sizeof(t.lines[0]), "%s %u  scene %d  player %p", g.active ? "PLAY MOVE STALLED" : "NO PLAYER",
                 g.stalled_draws, play->scene_id, (void*)GET_PLAYER_ACTOR(play));
        snprintf(t.lines[1], sizeof(t.lines[1]), "fb_mode %d wipe_mode %d fade %d wipe_type %d", play->fb_mode,
                 play->fb_wipe_mode, play->fb_fade_type, play->fbdemo_wipe.wipe_type);
        snprintf(t.lines[2], sizeof(t.lines[2]), "submenu status %d mode %d  paused %d", play->submenu.process_status,
                 play->submenu.mode, g_pc_paused);
        t.count = 3;
        HcView v = g.view;
        v.first_person = 0;
        hc_draw_hud(play, &g.sim, &v, &t);
        return;
    }
    if (!g.active) return;
    hc_draw_hud(play, &g.sim, &g.view, &g.hud);
}

int hc_hook_sdl_event(const union SDL_Event* e) {
    hc_init_once();
    return hc_input_event(&g.input, e);
}

void hc_hook_filter_pad(struct PADStatus* status) {
    if (status == NULL) return;
    if (g.attract && !g.input.capture && g.active && dialogue_open()) {
        /* The tour can't read; tap A through whatever it's being told. */
        static unsigned frame;
        status->button = (++frame % 16) < 4 ? PAD_BUTTON_A : 0;
        return;
    }
    if (!g.fps_live) return;
    status->button = 0;
    status->stickX = status->stickY = 0;
    status->substickX = status->substickY = 0;
    status->triggerLeft = status->triggerRight = 0;
    status->analogA = status->analogB = 0;
}
