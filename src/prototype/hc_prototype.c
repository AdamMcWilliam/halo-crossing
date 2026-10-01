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
#include "hc_gfx.h"
#include "hc_input.h"
#include "hc_world_scale.h"
#include "m_msg.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_view.h"

extern int g_pc_paused;

#define HC_FOV_Y 55.0f     /* Halo CE's 70 degree horizontal FOV at 4:3 */
#define HC_NEAR 1.0f
#define HC_FAR 3200.0f
#define HC_SPAWN_AHEAD 6.0f
#define HC_LOG_LINES 3
#define HC_LOG_LIFE 6.0f

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
    static const char* names[HALO_BIPED_COUNT] = { "chief", "grunt", "elite" };
    if (ui < 0 || ui >= HALO_MAX_UNITS) return "world";
    return names[g.sim.units[ui].biped];
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
            if (e->unit != player) g.kills++;
            hc_log("%s killed by %s", unit_name(e->unit), unit_name(e->other));
            break;
        case HALO_EV_AI_ALERTED:
            hc_log("%s alerted", unit_name(e->unit));
            break;
        case HALO_EV_AI_PANIC:
            hc_log("%s panics", unit_name(e->unit));
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
}

static int dialogue_open(void) {
    mMsg_Window_c* w = mMsg_Get_base_window_p();
    return w != NULL && !mMsg_Check_MainHide(w);
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
    a->speed = 0.0f;
    a->position_speed.x = a->position_speed.y = a->position_speed.z = 0.0f;
    s16 yaw = hc_h2a_yaw(p->yaw);
    a->shape_info.rotation.y = yaw;
    a->world.angle.y = yaw;
}

static void handle_fkeys(void) {
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
            halo_give_weapon(&g.sim, g.sim.player, HALO_WEAPON_ASSAULT_RIFLE);
            p->grenades[HALO_GRENADE_PLASMA] = g_halo_grenades[HALO_GRENADE_PLASMA].maximum_count;
            hc_message("ASSAULT RIFLE");
        }
    }
    if (hc_input_take_fkey(in, 8)) {
        g.sim.infinite_shields = !g.sim.infinite_shields;
        hc_message(g.sim.infinite_shields ? "INFINITE SHIELDS ON" : "INFINITE SHIELDS OFF");
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
        snprintf(t->lines[t->count++], sizeof(t->lines[0]), "%s %d/%d  sh %.0f  hp %.0f  gren %d%s", wn,
                 p->weapon.rounds_loaded, p->weapon.rounds_reserve, p->shield, p->body,
                 p->grenades[HALO_GRENADE_PLASMA], g.sim.infinite_shields ? "  [INF SH]" : "");
    }
    char ai[96];
    int n = 0, len = 0;
    ai[0] = 0;
    for (int i = 0; i < HALO_MAX_UNITS && len < 80; i++) {
        if (!g.sim.ai[i].active || !g.sim.units[i].active) continue;
        if (!g.sim.units[i].dead) n++;
        len += snprintf(ai + len, sizeof(ai) - len, " %c:%s", g_halo_actors[g.sim.ai[i].type].name[0],
                        halo_ai_state_name(g.sim.ai[i].state));
    }
    snprintf(t->lines[t->count++], sizeof(t->lines[0]), "covenant %d  kills %d%s", n, g.kills, ai);
    snprintf(t->lines[t->count++], sizeof(t->lines[0]), "gfx %d/%d/%d ovf %d  rays %d lc %d path %d", gs->opa_used,
             gs->xlu_used, gs->font_used, gs->overflows, ws->rays, ws->line_checks, ws->paths);
    for (int i = 0; i < HC_LOG_LINES && t->count < HC_DEBUG_LINES; i++) {
        if (g.log[i][0] && g.log_age[i] < HC_LOG_LIFE) snprintf(t->lines[t->count++], sizeof(t->lines[0]), "> %s", g.log[i]);
    }
}

void hc_hook_play_init(GAME_PLAY* play) {
    (void)play;
    hc_init_once();
    halo_sim_init(&g.sim, hc_ac_world_api(), (unsigned int)SDL_GetTicks() | 1u);
    hc_ac_world_invalidate();
    hc_fx_clear();
    g.active = 0;
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
    g.fps += (1.0f / dt - g.fps) * 0.05f;
    g.view.time = g.time;

    PLAYER_ACTOR* pl = GET_PLAYER_ACTOR(play);
    g.active = pl != NULL;
    if (!g.active) {
        g.fps_live = 0;
        hc_input_set_capture(&g.input, 0);
        return;
    }

    handle_fkeys();
    g.fps_live = g.first_person && !dialogue_open() && !g_pc_paused;
    hc_input_set_capture(&g.input, g.fps_live && !g.no_capture && SDL_GetKeyboardFocus() != NULL);

    HaloUnit* p = ensure_player(pl);
    if (p == NULL) return;
    if (g.fps_live && !p->dead) {
        hc_input_apply(&g.input, &p->control);
    } else if (!g.fps_live) {
        sync_halo_from_ac(pl, p);
    }

    if (g.autospawn_pending && g.fps_live) {
        g.autospawn_pending = 0;
        spawn_ahead(HALO_ACTOR_GRUNT, HC_SPAWN_AHEAD, 0.0f);
    }

    HcAcWorldStats* ws = hc_ac_world_stats();
    memset(ws, 0, sizeof(*ws));
    halo_sim_advance(&g.sim, dt);

    p = halo_player(&g.sim);
    if (g.fps_live && p) sync_ac_from_halo(pl, p);
    pl->actor_class.skip_drawing = (u8)(g.fps_live ? TRUE : FALSE);

    for (int i = 0; i < g.sim.event_count; i++) {
        hc_fx_from_event(&g.sim, &g.sim.events[i]);
        log_event(&g.sim.events[i]);
    }
    halo_sim_clear_events(&g.sim);
    for (int i = 0; i < HC_LOG_LINES; i++) g.log_age[i] += dt;
    hc_fx_update(dt);

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
    setPerspectiveView(&play->view, HC_FOV_Y, HC_NEAR, HC_FAR);
    setLookAtView(&play->view, &e, &c, &up);

    g.view.first_person = 1;
    g.view.eye = e;
    g.view.yaw = yaw;
    g.view.pitch = pitch;
    hc_draw_sky(play, &g.view, HC_FOV_Y);
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
    if (!g.fps_live || status == NULL) return;
    status->button = 0;
    status->stickX = status->stickY = 0;
    status->substickX = status->substickY = 0;
    status->triggerLeft = status->triggerRight = 0;
    status->analogA = status->analogB = 0;
}
