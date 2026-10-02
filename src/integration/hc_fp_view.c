#include "hc_fp_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hc_fp_pack.h"
#include "hc_gfx.h"
#include "hc_world_scale.h"
#include "libforest/gbi_extensions.h"
#include "m_lib.h"
#include "sys_matrix.h"

#ifndef HC_HALO_ASSET_DIR
#define HC_HALO_ASSET_DIR "assets_local/halo/generated"
#endif

#define PACK_NAME "fp_weapons.hcpk"
#define MAX_VERTICES 8192
#define POS_Q 4096.0f           /* first-person units per wu in Vtx positions */
#define GX_WRAP_REPEAT 1        /* GXTexWrapMode GX_REPEAT */
#define OPA_RESERVE 8192        /* bytes kept free for the rest of the opaque list */
#define FIRE_HOLD 0.2f          /* seconds after a shot that automatic fire keeps looping */
#define SUN_SHARE 0.6f          /* how much of AC's sun light reaches the held weapon */
#define TINT_FLOOR 0.3f

#define CC_TEX_SHADE TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0
#define CC_SHADE_PRIM SHADE, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE
#define CC_TEX_PRIM TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0

typedef struct Track {
    HcFpAnim anim;
    float frame;
    float rate;    /* 1 = Halo's 30 fps */
    int loop;
} Track;

static struct {
    int tried;
    int loaded;
    HcFpPack pack;
    char status[192];

    int weapon;
    Track cur, prev;
    float fade, fade_time;
    float idle_time, posing_after;
    int reloading;
    int reload_full;
    float last_grenade_timer;
    float since_fire;
    int fired, fired_charged;

    HcFpXf local[HC_FP_MAX_NODES];
    HcFpMtx world[HC_FP_MAX_NODES];
    HcFpMtx skin[HC_FP_MAX_NODES];
    int posed;

    float pos[MAX_VERTICES][3];
    u8 shade[MAX_VERTICES];
    float tint[3];
} s;

void hc_fp_view_init(void) {
    if (s.tried) return;
    s.tried = 1;
    s.weapon = -1;
    s.tint[0] = s.tint[1] = s.tint[2] = 1.0f;
    const char* dir = getenv("HC_HALO_ASSETS");
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/%s", dir && dir[0] ? dir : HC_HALO_ASSET_DIR, PACK_NAME);
    if (hc_fp_pack_load(&s.pack, path, "HCFP", HC_FP_WEAPON_SLOTS, HC_FP_ANIM_COUNT, err, sizeof(err))) {
        int n = 0;
        for (int i = 0; i < HC_FP_WEAPON_SLOTS; i++) n += s.pack.has[i];
        s.loaded = 1;
        snprintf(s.status, sizeof(s.status), "Halo first-person models: %d weapons", n);
    } else {
        snprintf(s.status, sizeof(s.status), "Halo first-person models: none (%s)", err);
    }
    printf("[halo-crossing] %s [%s]\n", s.status, path);
    fflush(stdout);
}

const char* hc_fp_view_status(void) {
    return s.status;
}

static const HcFpModel* weapon_for(HaloWeaponId id) {
    return s.loaded ? hc_fp_pack_model(&s.pack, (int)id) : NULL;
}

int hc_fp_view_available(HaloWeaponId id) {
    return weapon_for(id) != NULL;
}

void hc_fp_view_event(const HaloSim* sim, const HaloEvent* e) {
    if (e->type != HALO_EV_WEAPON_FIRED || e->unit < 0 || !sim->units[e->unit].is_player) return;
    s.fired = 1;
    s.fired_charged = e->value > 0.5f;
}

/* ---- animation ------------------------------------------------------------- */

static int has(const HcFpModel* w, HcFpAnim a) {
    return w->anim_frames[a] > 0;
}

static float last_frame(const HcFpModel* w, HcFpAnim a) {
    return (float)(w->anim_frames[a] > 0 ? w->anim_frames[a] - 1 : 0);
}

static int done(const HcFpModel* w, const Track* t) {
    return !t->loop && t->frame >= last_frame(w, t->anim);
}

static void play(const HcFpModel* w, HcFpAnim a, float rate, int loop, float fade) {
    if (!has(w, a)) return;
    s.prev = s.cur;
    s.cur.anim = a;
    s.cur.frame = 0.0f;
    s.cur.rate = rate;
    s.cur.loop = loop;
    s.fade_time = fade;
    s.fade = fade > 0.0f ? 0.0f : 1.0f;
}

/* Rate that stretches an animation over `seconds`. */
static float rate_for(const HcFpModel* w, HcFpAnim a, float seconds) {
    if (seconds <= 0.0f || !has(w, a)) return 1.0f;
    return last_frame(w, a) / (seconds * HC_FP_FPS);
}

static void choose(const HcFpModel* w, const HaloUnit* p, float dt) {
    const HaloWeaponState* ws = &p->weapon;
    const HaloWeaponDef* wd = &g_halo_weapons[ws->id];
    int fired = s.fired, charged = s.fired_charged;
    s.fired = s.fired_charged = 0;
    s.since_fire = fired ? 0.0f : s.since_fire + dt;

    if (ws->ready_timer > 0.0f && wd->ready_time > 0.0f) {
        if (s.cur.anim != HC_FP_READY) play(w, HC_FP_READY, 1.0f, 0, 0.0f);
        s.cur.frame = (1.0f - ws->ready_timer / wd->ready_time) * last_frame(w, HC_FP_READY);
        return;
    }

    if (ws->reload_timer > 0.0f && wd->reload_time > 0.0f) {
        if (wd->reload_per_round) {
            if (!s.reloading) {
                s.reloading = 1;
                if (has(w, HC_FP_RELOAD_ENTER)) play(w, HC_FP_RELOAD_ENTER, 1.0f, 0, 0.08f);
                else play(w, HC_FP_RELOAD_EMPTY, rate_for(w, HC_FP_RELOAD_EMPTY, wd->reload_time), 1, 0.08f);
            } else if (s.cur.anim != HC_FP_RELOAD_EMPTY && done(w, &s.cur)) {
                play(w, HC_FP_RELOAD_EMPTY, rate_for(w, HC_FP_RELOAD_EMPTY, wd->reload_time), 1, 0.05f);
            }
            return;
        }
        if (!s.reloading) {
            s.reloading = 1;
            s.reload_full = ws->rounds_loaded > 0;
            play(w, s.reload_full ? HC_FP_RELOAD_FULL : HC_FP_RELOAD_EMPTY, 1.0f, 0, 0.1f);
        }
        if (s.cur.anim == HC_FP_RELOAD_FULL || s.cur.anim == HC_FP_RELOAD_EMPTY)
            s.cur.frame = (1.0f - ws->reload_timer / wd->reload_time) * last_frame(w, s.cur.anim);
        return;
    }
    if (s.reloading) {
        s.reloading = 0;
        if (wd->reload_per_round && !fired) play(w, HC_FP_RELOAD_EXIT, 1.0f, 0, 0.06f);
    }

    int throwing = s.cur.anim == HC_FP_THROW_GRENADE && !done(w, &s.cur);
    if (p->grenade_timer > 0.0f && s.last_grenade_timer <= 0.0f) {
        play(w, HC_FP_THROW_GRENADE, 1.0f, 0, 0.08f);
        return;
    }
    if (throwing) return;

    if (fired) {
        HcFpAnim a = charged && has(w, HC_FP_FIRE_CHARGED) ? HC_FP_FIRE_CHARGED : HC_FP_FIRE;
        /* Automatic fire lets the recoil cycle run instead of restarting it every round. */
        int cycling = wd->trigger.automatic && s.cur.anim == a && !done(w, &s.cur);
        if (!cycling) play(w, a, 1.0f, 0, s.cur.anim == a ? 0.0f : 0.05f);
        return;
    }
    if ((s.cur.anim == HC_FP_FIRE || s.cur.anim == HC_FP_FIRE_CHARGED) && done(w, &s.cur) &&
        wd->trigger.automatic && s.since_fire < FIRE_HOLD) {
        s.cur.frame = 0.0f;
        return;
    }

    if (ws->overheated && has(w, HC_FP_OVERHEATED)) {
        if (s.cur.anim != HC_FP_OVERHEATED) play(w, HC_FP_OVERHEATED, 1.0f, 1, 0.12f);
        return;
    }
    if (s.cur.anim == HC_FP_OVERHEATED || done(w, &s.cur)) {
        play(w, HC_FP_IDLE, 1.0f, 1, 0.2f);
        s.idle_time = 0.0f;
    }
    if (s.cur.anim == HC_FP_IDLE) {
        s.idle_time += dt;
        if (s.idle_time > s.posing_after && has(w, HC_FP_POSING)) {
            play(w, HC_FP_POSING, 1.0f, 0, 0.3f);
            s.idle_time = 0.0f;
            s.posing_after = 18.0f + (float)(rand() % 12);
        }
    }
}

void hc_fp_view_update(const HaloUnit* p, float dt) {
    s.posed = 0;
    if (p == NULL || p->dead) return;
    const HcFpModel* w = weapon_for(p->weapon.id);
    if (w == NULL) {
        s.weapon = -1;
        return;
    }
    if (dt > 0.1f) dt = 0.1f;
    if ((int)p->weapon.id != s.weapon) {
        s.weapon = (int)p->weapon.id;
        memset(&s.cur, 0, sizeof(s.cur));
        s.cur.loop = 1;
        s.cur.rate = 1.0f;
        s.prev = s.cur;
        s.fade = 1.0f;
        s.reloading = 0;
        s.idle_time = 0.0f;
        s.posing_after = 14.0f;
        s.since_fire = FIRE_HOLD;
        play(w, HC_FP_READY, 1.0f, 0, 0.0f);
    }

    float step = dt * HC_FP_FPS;
    s.cur.frame += step * s.cur.rate;
    s.prev.frame += step * s.prev.rate;
    if (s.fade < 1.0f) s.fade = s.fade_time > 0.0f ? hc_minf(1.0f, s.fade + dt / s.fade_time) : 1.0f;
    choose(w, p, dt);
    s.last_grenade_timer = p->grenade_timer;

    hc_fp_sample(w, s.cur.anim, s.cur.frame, s.cur.loop, s.local);
    if (s.fade < 1.0f) {
        static HcFpXf from[HC_FP_MAX_NODES];
        hc_fp_sample(w, s.prev.anim, s.prev.frame, s.prev.loop, from);
        hc_fp_blend(from, s.local, s.fade, w->node_count);
        memcpy(s.local, from, sizeof(HcFpXf) * (size_t)w->node_count);
    }
    hc_fp_pose(w, s.local, s.world, s.skin);
    s.posed = 1;
}

int hc_fp_view_muzzle(const HaloUnit* p, float out[3]) {
    const HcFpModel* w = p ? weapon_for(p->weapon.id) : NULL;
    if (w == NULL || !s.posed || (int)p->weapon.id != s.weapon || w->muzzle_node < 0) return 0;
    hc_fp_mtx_point(s.world[w->muzzle_node], w->muzzle, out);
    return 1;
}

/* ---- drawing --------------------------------------------------------------- */

static const HcFpModel* drawable(const HcView* v, const HaloUnit* p) {
    if (p == NULL || p->dead || v->zoom > 1.01f || !s.posed) return NULL;
    const HcFpModel* w = weapon_for(p->weapon.id);
    return (w && (int)p->weapon.id == s.weapon && w->vertex_count <= MAX_VERTICES) ? w : NULL;
}

static Gfx* load_matrix(Gfx* g, GRAPH* graph, const HcView* v) {
    float k = HALO_TO_AC_SCALE * HC_FP_VIEW_SCALE / POS_Q;
    Matrix_translate(v->eye.x, v->eye.y, v->eye.z, MTX_LOAD);
    Matrix_RotateY(hc_h2a_yaw(v->yaw), MTX_MULT);
    Matrix_RotateX(HC_RAD_TO_S16(-v->pitch), MTX_MULT);
    Matrix_scale(k, k, k, MTX_MULT);
    Mtx* m = _Matrix_to_Mtx_new(graph);
    if (m == NULL) return NULL;
    gSPMatrix(g++, m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    return g;
}

/* Vertices come from the tail of the opaque arena; leave room for its commands. */
static Vtx* alloc_vtx(GRAPH* graph, const Gfx* opa_head, int n) {
    THA_GA* t = &graph->polygon_opaque_thaga;
    const char* head = (const char*)(opa_head ? opa_head : t->thaGfx.head_p);
    const char* tail = (const char*)t->tha.tail_p;
    if (tail - head < (ptrdiff_t)(sizeof(Vtx) * (size_t)n + OPA_RESERVE)) return NULL;
    return GRAPH_ALLOC_TYPE(graph, Vtx, n);
}

static s16 to_s16(float x) {
    return (s16)(x > 32767.0f ? 32767 : (x < -32768.0f ? -32768 : (int)lrintf(x)));
}

static Gfx* set_texture(Gfx* g, int index) {
    const HcFpTexture* t = &s.pack.textures[index];
    int siz = t->format == HC_FP_TEXTURE_CMPR ? G_IM_SIZ_4b : G_IM_SIZ_32b;
    gDPPipeSync(g++);
    /* The macro takes height before width. */
    gDPSetTextureImage_Dolphin(g++, G_IM_FMT_RGBA, siz, t->height, t->width, hc_fp_texture_pixels(&s.pack, index));
    gDPSetTile_Dolphin(g++, G_DOLPHIN_TLUT_DEFAULT_MODE, G_TX_RENDERTILE, 0, GX_WRAP_REPEAT, GX_WRAP_REPEAT, 0, 0);
    return g;
}

/* Skin every vertex once per frame and light it with a key light over the
 * left shoulder. */
static void skin_all(const HcFpModel* w, const HcView* v) {
    static const float light[3] = { 0.28f, 0.46f, 0.84f };
    float bob_l = sinf(v->bob_phase) * 0.006f * v->bob_amount;
    float bob_u = -fabsf(cosf(v->bob_phase)) * 0.005f * v->bob_amount;
    for (int i = 0; i < w->vertex_count; i++) {
        float n[3];
        hc_fp_skin_vertex(s.skin, &w->vertices[i], s.pos[i], n);
        s.pos[i][1] += bob_l;
        s.pos[i][2] += bob_u;
        float d = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
        float lit = 0.46f + 0.62f * (d > 0.0f ? d : 0.0f) + 0.12f * (n[2] > 0.0f ? n[2] : 0.0f);
        s.shade[i] = (u8)(lit >= 1.0f ? 255 : (int)(lit * 255.0f));
    }
}

static Gfx* draw_batch(Gfx* g, GRAPH* graph, const Gfx* opa_head, const HcFpModel* w, const HcFpBatch* b) {
    Vtx* vb = alloc_vtx(graph, opa_head, b->vertex_count);
    if (vb == NULL) return g;
    const uint16_t* idx = hc_fp_batch_indices(w, b);
    float tw = 0.0f, th = 0.0f;
    if (b->texture != HC_FP_NO_TEXTURE) {
        tw = (float)s.pack.textures[b->texture].width * 32.0f;
        th = (float)s.pack.textures[b->texture].height * 32.0f;
    }
    for (int k = 0; k < b->vertex_count; k++) {
        int i = idx[k];
        const float* p = s.pos[i];
        Vtx* o = &vb[k];
        /* First-person (forward, left, up) -> AC model (left, up, forward). */
        o->v.ob[0] = to_s16(p[1] * POS_Q);
        o->v.ob[1] = to_s16(p[2] * POS_Q);
        o->v.ob[2] = to_s16(p[0] * POS_Q);
        o->v.flag = 0;
        o->v.tc[0] = to_s16((w->vertices[i].uv[0] - b->uv_offset[0]) * tw);
        o->v.tc[1] = to_s16((w->vertices[i].uv[1] - b->uv_offset[1]) * th);
        for (int c = 0; c < 3; c++) {
            float lit = (float)s.shade[i] * s.tint[c];
            o->v.cn[c] = (u8)(lit >= 255.0f ? 255 : (int)lit);
        }
        o->v.cn[3] = 255;
    }
    gSPVertex(g++, vb, b->vertex_count, 0);
    const uint8_t* tri = hc_fp_batch_triangles(w, b);
    int n = b->triangle_count, t = 0;
    for (; t + 1 < n; t += 2) {
        const uint8_t* a = tri + t * 3;
        gSP2Triangles(g++, a[0], a[1], a[2], 0, a[3], a[4], a[5], 0);
    }
    if (t < n) gSP1Triangle(g++, tri[t * 3], tri[t * 3 + 1], tri[t * 3 + 2], 0);
    return g;
}

/* AC's ambient and sun colours this frame as a tint on the key-lit grey:
 * 1 at AC's midday (ambient 80 80 150, sun 200 240 240), warm at dusk,
 * dark blue at night but never so dark the weapon is lost. */
static void light_tint(GAME_PLAY* play) {
    static const float midday[3] = { 80.0f + 200.0f * SUN_SHARE, 80.0f + 240.0f * SUN_SHARE,
                                     150.0f + 240.0f * SUN_SHARE };
    const LightDiffuse* sun = &play->kankyo.sun_light.lights.diffuse;
    const u8* amb = play->global_light.ambientColor;
    for (int c = 0; c < 3; c++) {
        float raw = ((float)amb[c] + (float)sun->color[c] * SUN_SHARE) / midday[c];
        s.tint[c] = TINT_FLOOR + (1.0f - TINT_FLOOR) * hc_clampf(raw, 0.0f, 1.15f);
    }
}

Gfx* hc_fp_view_draw_opa(Gfx* g, GRAPH* graph, GAME_PLAY* play, const HcView* v, const HaloUnit* p) {
    const HcFpModel* w = drawable(v, p);
    if (w == NULL) return g;
    light_tint(play);
    skin_all(w, v);
    Gfx* start = g;
    g = load_matrix(g, graph, v);
    if (g == NULL) return start;

    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    int tex = -2;
    for (int i = 0; i < w->batch_count; i++) {
        const HcFpBatch* b = &w->batches[i];
        if (b->flags & HC_FP_BATCH_TRANSLUCENT) continue;
        if (b->texture != tex) {
            tex = b->texture;
            if (tex == HC_FP_NO_TEXTURE) {
                gDPPipeSync(g++);
                gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
                gDPSetCombineMode(g++, CC_SHADE_PRIM, CC_SHADE_PRIM);
            } else {
                g = set_texture(g, tex);
                gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
                gDPSetCombineMode(g++, CC_TEX_SHADE, CC_TEX_SHADE);
            }
        }
        if (tex == HC_FP_NO_TEXTURE)
            gDPSetPrimColor(g++, 0, 0, HC_R(b->rgba), HC_G(b->rgba), HC_B(b->rgba), 255);
        g = draw_batch(g, graph, g, w, b);
    }
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    return hc_gfx_mode_opa(g);
}

Gfx* hc_fp_view_draw_xlu(Gfx* g, GRAPH* graph, const HcView* v, const HaloUnit* p) {
    const HcFpModel* w = drawable(v, p);
    if (w == NULL) return g;
    int any = 0;
    for (int i = 0; i < w->batch_count && !any; i++) any = (w->batches[i].flags & HC_FP_BATCH_TRANSLUCENT) != 0;
    if (!any) return g;
    Gfx* start = g;
    g = load_matrix(g, graph, v);
    if (g == NULL) return start;

    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_ZB_XLU_SURF | G_RM_ZB_XLU_SURF2);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    int tex = -2;
    for (int i = 0; i < w->batch_count; i++) {
        const HcFpBatch* b = &w->batches[i];
        if (!(b->flags & HC_FP_BATCH_TRANSLUCENT)) continue;
        if (b->texture != tex) {
            tex = b->texture;
            if (tex == HC_FP_NO_TEXTURE) {
                gDPPipeSync(g++);
                gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
                gDPSetCombineMode(g++, CC_SHADE_PRIM, CC_SHADE_PRIM);
            } else {
                g = set_texture(g, tex);
                gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
                gDPSetCombineMode(g++, CC_TEX_PRIM, CC_TEX_PRIM);
            }
        }
        gDPSetPrimColor(g++, 0, 0, HC_R(b->rgba), HC_G(b->rgba), HC_B(b->rgba), HC_A(b->rgba));
        g = draw_batch(g, graph, NULL, w, b);
    }
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    return hc_gfx_mode_xlu(g);
}
