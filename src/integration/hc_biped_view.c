#include "hc_biped_view.h"

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

#define POS_Q 8192.0f           /* Vtx units per wu of model space */
#define MAX_SKIN 4096           /* one unit's body + weapon at full detail */
#define VTX_POOL 98304          /* every unit drawn in a frame */
#define GX_WRAP_REPEAT 1        /* GXTexWrapMode GX_REPEAT */
#define LOD1_AC 320.0f          /* ~6.7 wu: beyond this, the low model */
#define LOD2_AC 760.0f          /* ~16 wu: beyond this, the lowest */
#define CULL_RADIUS_AC 60.0f
#define FADE_TIME 0.18f
#define PING_DAMAGE 6.0f        /* one hit this hard (unshielded) makes them flinch */
#define PING_COOLDOWN 1.4f
#define MOVE_SPEED 0.15f        /* wu/s; slower counts as standing */
#define SHIELD_RGB 0x9CD8FF00
#define HULL_PUSH 0.012f        /* wu the shield flare floats off the skin */
#define MAX_BLASTS 16

#define CC_TEX_SHADE TEXEL0, 0, SHADE, 0, 0, 0, 0, TEXEL0
#define CC_SHADE_PRIM SHADE, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE
#define CC_TEX_PRIM TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0

typedef struct Track {
    int anim;
    float frame;
    float rate;     /* 1 = Halo's 30 fps */
    int loop;
} Track;

typedef struct UnitAnim {
    int serial;     /* HaloUnit serial this state belongs to */
    Track cur, prev;
    float fade, fade_time;
    int oneshot;    /* cur owns the body until it ends */
    int death;      /* chosen death animation, -1 alive */
    int fire_anim;
    float fire_frame; /* overlay playhead, < 0 none */
    float since_ping;
    float airborne_time;
    int was_airborne;
    float last_grenade_timer;
    int want;       /* one-shot asked for by an event, -1 none */
} UnitAnim;

typedef struct Blast {
    hv3 pos;
    float radius;
} Blast;

static struct {
    int tried, loaded;
    HcFpPack pack;
    char status[160];
    UnitAnim units[HALO_MAX_UNITS];
    Blast blasts[MAX_BLASTS];
    int blast_count;

    HcFpXf local[HC_FP_MAX_NODES], from[HC_FP_MAX_NODES];
    HcFpMtx world[HC_FP_MAX_NODES], skin[HC_FP_MAX_NODES];
    float pos[MAX_SKIN][3], nrm[MAX_SKIN][3];
    u8 rgb[MAX_SKIN][3];

    Vtx pool[2][VTX_POOL];
    int pool_flip, pool_used;
    HcBipedStats stats;
} s;

void hc_biped_view_init(void) {
    if (s.tried) return;
    s.tried = 1;
    const char* dir = getenv("HC_HALO_ASSETS");
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/%s", dir && dir[0] ? dir : HC_HALO_ASSET_DIR, HC_BP_PACK_NAME);
    if (hc_fp_pack_load(&s.pack, path, "HCBP", HC_BP_MODEL_COUNT, HC_BP_ANIM_COUNT, err, sizeof(err))) {
        s.loaded = 1;
        snprintf(s.status, sizeof(s.status), "Halo Covenant models: %s%s",
                 s.pack.has[HC_BP_GRUNT] ? "grunt " : "", s.pack.has[HC_BP_ELITE] ? "elite" : "");
    } else {
        snprintf(s.status, sizeof(s.status), "Halo Covenant models: none (%s)", err);
    }
    printf("[halo-crossing] %s [%s]\n", s.status, path);
    fflush(stdout);
}

const char* hc_biped_view_status(void) {
    return s.status;
}

HcBipedStats* hc_biped_view_stats(void) {
    return &s.stats;
}

static const HcFpModel* model_for(const HaloUnit* u) {
    if (!s.loaded) return NULL;
    switch (u->biped) {
        case HALO_BIPED_GRUNT: return hc_fp_pack_model(&s.pack, HC_BP_GRUNT);
        case HALO_BIPED_ELITE: return hc_fp_pack_model(&s.pack, HC_BP_ELITE);
        default: return NULL;
    }
}

static const HcFpModel* weapon_for(HaloWeaponId id) {
    switch (id) {
        case HALO_WEAPON_PLASMA_PISTOL: return hc_fp_pack_model(&s.pack, HC_BP_PLASMA_PISTOL);
        case HALO_WEAPON_PLASMA_RIFLE: return hc_fp_pack_model(&s.pack, HC_BP_PLASMA_RIFLE);
        case HALO_WEAPON_NEEDLER: return hc_fp_pack_model(&s.pack, HC_BP_NEEDLER);
        case HALO_WEAPON_FUEL_ROD: return hc_fp_pack_model(&s.pack, HC_BP_FUEL_ROD);
        default: return NULL;
    }
}

static int fire_overlay_for(HaloWeaponId id) {
    switch (id) {
        case HALO_WEAPON_PLASMA_RIFLE: return HC_BP_FIRE_RIFLE;
        case HALO_WEAPON_NEEDLER: return HC_BP_FIRE_NEEDLER;
        default: return HC_BP_FIRE_PISTOL;
    }
}

int hc_biped_view_available(const HaloUnit* u) {
    return model_for(u) != NULL;
}

/* ---- animation ------------------------------------------------------------- */

static UnitAnim* state_for(const HaloSim* sim, int i) {
    UnitAnim* a = &s.units[i];
    if (a->serial != sim->units[i].serial) {
        memset(a, 0, sizeof(*a));
        a->serial = sim->units[i].serial;
        a->cur.rate = a->prev.rate = 1.0f;
        a->cur.loop = a->prev.loop = 1;
        a->fade = 1.0f;
        a->death = -1;
        a->fire_frame = -1.0f;
        a->want = -1;
        a->since_ping = PING_COOLDOWN;
        a->cur.frame = (float)(i * 7);  /* don't idle in lockstep */
    }
    return a;
}

static float last_frame(const HcFpModel* m, int anim) {
    return (float)(m->anim_frames[anim] > 0 ? m->anim_frames[anim] - 1 : 0);
}

static int done(const HcFpModel* m, const Track* t) {
    return !t->loop && t->frame >= last_frame(m, t->anim);
}

static void play(UnitAnim* a, int anim, float rate, int loop, float fade) {
    a->prev = a->cur;
    a->cur.anim = anim;
    a->cur.frame = 0.0f;
    a->cur.rate = rate;
    a->cur.loop = loop;
    a->fade_time = fade;
    a->fade = fade > 0.0f ? 0.0f : 1.0f;
}

static void oneshot(UnitAnim* a, const HcFpModel* m, int anim) {
    if (!hc_fp_has_anim(m, anim)) return;
    play(a, anim, 1.0f, 0, 0.1f);
    a->oneshot = 1;
}

/* Relative bearing of a world direction from the unit's facing: 0 ahead,
 * +pi/2 on its left. */
static float bearing(const HaloUnit* u, hv3 d) {
    if (d.x == 0.0f && d.y == 0.0f) return 0.0f;
    return hc_wrap_angle(atan2f(d.y, d.x) - u->yaw);
}

static int pick_death(const HcFpModel* m, const HaloUnit* u) {
    for (int k = 0; k < s.blast_count; k++) {
        hv3 to = hv3_sub(s.blasts[k].pos, u->pos);
        if (hv3_len(to) > s.blasts[k].radius + 0.3f) continue;
        int a = fabsf(bearing(u, to)) < HC_PI * 0.5f ? HC_BP_DIE_HARD_FRONT : HC_BP_DIE_HARD_BACK;
        if (hc_fp_has_anim(m, a)) return a;
    }
    float b = bearing(u, u->last_damage_dir);
    int a;
    if (fabsf(b) <= HC_PI * 0.25f) a = HC_BP_DIE_FRONT;
    else if (fabsf(b) >= HC_PI * 0.75f) a = HC_BP_DIE_BACK;
    else a = b > 0.0f ? HC_BP_DIE_LEFT : HC_BP_DIE_RIGHT;
    return hc_fp_has_anim(m, a) ? a : HC_BP_DIE_FRONT;
}

void hc_biped_view_event(const HaloSim* sim, const HaloEvent* e) {
    if (!s.loaded) return;
    if (e->type == HALO_EV_EXPLOSION && e->value > 0.0f && s.blast_count < MAX_BLASTS) {
        s.blasts[s.blast_count].pos = e->pos;
        s.blasts[s.blast_count].radius = e->value;
        s.blast_count++;
        return;
    }
    if (e->unit < 0 || e->unit >= HALO_MAX_UNITS) return;
    const HaloUnit* u = &sim->units[e->unit];
    const HcFpModel* m = model_for(u);
    if (m == NULL || u->is_player || u->kinematic) return;
    UnitAnim* a = state_for(sim, e->unit);
    switch (e->type) {
        case HALO_EV_WEAPON_FIRED:
            a->fire_anim = fire_overlay_for(u->weapon.id);
            a->fire_frame = 0.0f;
            break;
        case HALO_EV_UNIT_DAMAGED:
            if (!u->dead && u->shield <= 0.0f && e->value >= PING_DAMAGE && a->since_ping >= PING_COOLDOWN) {
                a->since_ping = 0.0f;
                a->want = fabsf(bearing(u, u->last_damage_dir)) < HC_PI * 0.5f ? HC_BP_PING_FRONT : HC_BP_PING_BACK;
            }
            break;
        case HALO_EV_AI_ALERTED:
            if (hv3_len_xy(u->vel) < MOVE_SPEED) a->want = HC_BP_SURPRISE;
            break;
        case HALO_EV_UNIT_KILLED:
            a->death = pick_death(m, u);
            break;
        default:
            break;
    }
}

static void choose(UnitAnim* a, const HcFpModel* m, const HaloSim* sim, int i, float dt) {
    const HaloUnit* u = &sim->units[i];
    if (u->dead) {
        if (a->death < 0) a->death = pick_death(m, u);
        if (a->cur.anim != a->death || !a->oneshot) oneshot(a, m, a->death);
        a->fire_frame = -1.0f;
        return;
    }

    a->airborne_time = u->grounded ? 0.0f : a->airborne_time + dt;
    if (u->grenade_timer > 0.0f && a->last_grenade_timer <= 0.0f) a->want = HC_BP_THROW_GRENADE;
    a->last_grenade_timer = u->grenade_timer;
    if (a->want >= 0) {
        if (!(a->oneshot && a->cur.anim == HC_BP_THROW_GRENADE && !done(m, &a->cur))) oneshot(a, m, a->want);
        a->want = -1;
    }
    if (a->oneshot) {
        if (!done(m, &a->cur)) return;
        a->oneshot = 0;
    }
    if (a->airborne_time > 0.25f && hc_fp_has_anim(m, HC_BP_AIRBORNE)) {
        a->was_airborne = 1;
        if (a->cur.anim != HC_BP_AIRBORNE) play(a, HC_BP_AIRBORNE, 1.0f, 1, FADE_TIME);
        return;
    }
    if (a->was_airborne && u->grounded) {
        a->was_airborne = 0;
        if (hc_fp_has_anim(m, HC_BP_LAND)) {
            oneshot(a, m, HC_BP_LAND);
            return;
        }
    }

    float speed = hv3_len_xy(u->vel);
    float cy = cosf(u->yaw), sy = sinf(u->yaw);
    float fwd = u->vel.x * cy + u->vel.y * sy, left = -u->vel.x * sy + u->vel.y * cy;
    int heavy = u->weapon.id == HALO_WEAPON_FUEL_ROD && hc_fp_has_anim(m, HC_BP_HEAVY_IDLE);
    int fleeing = sim->ai[i].active && sim->ai[i].state == HALO_AI_FLEE;
    int anim, moving = speed > MOVE_SPEED;
    if (fleeing && moving && hc_fp_has_anim(m, HC_BP_FLEE)) {
        anim = HC_BP_FLEE;
    } else if (u->crouch_blend > 0.5f) {
        anim = moving ? HC_BP_CROUCH_MOVE : HC_BP_CROUCH_IDLE;
    } else if (moving) {
        if (fabsf(fwd) >= fabsf(left)) anim = fwd >= 0.0f ? HC_BP_MOVE_FRONT : HC_BP_MOVE_BACK;
        else anim = left > 0.0f ? HC_BP_MOVE_LEFT : HC_BP_MOVE_RIGHT;
        if (heavy && hc_fp_has_anim(m, anim + (HC_BP_HEAVY_MOVE_FRONT - HC_BP_MOVE_FRONT)))
            anim += HC_BP_HEAVY_MOVE_FRONT - HC_BP_MOVE_FRONT;
    } else if (fleeing && hc_fp_has_anim(m, HC_BP_WARN)) {
        anim = HC_BP_WARN;
    } else {
        anim = heavy ? HC_BP_HEAVY_IDLE : HC_BP_IDLE;
    }
    if (!hc_fp_has_anim(m, anim)) anim = moving ? HC_BP_MOVE_FRONT : HC_BP_IDLE;
    if (!hc_fp_has_anim(m, anim)) anim = HC_BP_IDLE;

    /* Stride to the ground speed so feet don't skate. */
    float rate = 1.0f;
    if (moving && m->anim_frames[anim] > 0) {
        float natural = m->anim_distance[anim] / (float)m->anim_frames[anim] * HC_FP_FPS;
        if (natural > 0.05f) rate = hc_clampf(speed / natural, 0.5f, 2.5f);
    }
    if (anim != a->cur.anim) play(a, anim, rate, 1, FADE_TIME);
    else a->cur.rate = rate;
}

void hc_biped_view_update(const HaloSim* sim, float dt) {
    if (!s.loaded) return;
    if (dt > 0.1f) dt = 0.1f;
    float step = dt * HC_FP_FPS;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        const HcFpModel* m = u->active && !u->is_player && !u->kinematic ? model_for(u) : NULL;
        if (m == NULL) {
            s.units[i].serial = 0;
            continue;
        }
        UnitAnim* a = state_for(sim, i);
        a->cur.frame += step * a->cur.rate;
        a->prev.frame += step * a->prev.rate;
        if (a->fade < 1.0f) a->fade = a->fade_time > 0.0f ? hc_minf(1.0f, a->fade + dt / a->fade_time) : 1.0f;
        if (a->fire_frame >= 0.0f) {
            a->fire_frame += step;
            if (!hc_fp_has_anim(m, a->fire_anim) || a->fire_frame > last_frame(m, a->fire_anim)) a->fire_frame = -1.0f;
        }
        a->since_ping += dt;
        choose(a, m, sim, i, dt);
    }
    s.blast_count = 0;
}

/* ---- drawing --------------------------------------------------------------- */

static float lerp_angle(float a, float b, float t) {
    return a + hc_wrap_angle(b - a) * t;
}

static s16 to_s16(float x) {
    return (s16)(x > 32767.0f ? 32767 : (x < -32768.0f ? -32768 : (int)lrintf(x)));
}

static int lod_first_batch(const HcFpModel* m, int lod) {
    return lod > 0 ? m->lod_batch_end[lod - 1] : 0;
}

static int lod_first_vertex(const HcFpModel* m, int lod) {
    return lod > 0 ? m->lod_vertex_end[lod - 1] : 0;
}

typedef struct UnitLight {
    float dir[3];       /* toward the sun, Halo model space */
    float ambient[3], sun[3];
    float flash;        /* 0..1 toward white */
} UnitLight;

static void shade(const UnitLight* L, const float n[3], u8 out[3]) {
    float d = n[0] * L->dir[0] + n[1] * L->dir[1] + n[2] * L->dir[2];
    if (d < 0.0f) d = 0.0f;
    for (int c = 0; c < 3; c++) {
        float v = L->ambient[c] + L->sun[c] * d;
        v += (255.0f - v) * L->flash;
        out[c] = (u8)(v >= 255.0f ? 255 : (v <= 0.0f ? 0 : (int)v));
    }
}

/* Skins the LOD's vertices into s.pos/nrm/rgb starting at `base`.
 * `attach`: rigid transform for a held weapon, or NULL for the skinned body. */
static int skin_range(const HcFpModel* m, int lod, int base, const HcFpMtx* attach, const UnitLight* L) {
    int v0 = lod_first_vertex(m, lod), v1 = m->lod_vertex_end[lod];
    if (base + (v1 - v0) > MAX_SKIN) return -1;
    for (int i = v0; i < v1; i++) {
        float* p = s.pos[base + i - v0];
        float* n = s.nrm[base + i - v0];
        const HcFpVertex* v = &m->vertices[i];
        if (attach) {
            const float (*a)[4] = *attach;
            for (int r = 0; r < 3; r++) {
                p[r] = a[r][0] * v->pos[0] + a[r][1] * v->pos[1] + a[r][2] * v->pos[2] + a[r][3];
                n[r] = a[r][0] * v->normal[0] + a[r][1] * v->normal[1] + a[r][2] * v->normal[2];
            }
        } else {
            hc_fp_skin_vertex(s.skin, v, p, n);
        }
        shade(L, n, s.rgb[base + i - v0]);
    }
    return v1 - v0;
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

enum { PASS_OPAQUE, PASS_BLENDED, PASS_HULL };

/* One model's batches for a pass. `base` is where skin_range put its first vertex. */
static Gfx* emit(Gfx* g, const HcFpModel* m, int lod, int base, int pass, u32 hull_rgba, int* tex) {
    int v0 = lod_first_vertex(m, lod);
    for (int bi = lod_first_batch(m, lod); bi < m->lod_batch_end[lod]; bi++) {
        const HcFpBatch* b = &m->batches[bi];
        int blended = (b->flags & HC_FP_BATCH_TRANSLUCENT) != 0;
        if ((pass == PASS_OPAQUE && blended) || (pass == PASS_BLENDED && !blended)) continue;
        if (s.pool_used + b->vertex_count > VTX_POOL) {
            s.stats.out_of_vertices++;
            break;
        }
        int want = pass == PASS_HULL ? HC_FP_NO_TEXTURE : b->texture;
        if (want != *tex) {
            *tex = want;
            if (want == HC_FP_NO_TEXTURE) {
                gDPPipeSync(g++);
                gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
                gDPSetCombineMode(g++, CC_SHADE_PRIM, CC_SHADE_PRIM);
            } else {
                g = set_texture(g, want);
                gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
                if (pass == PASS_BLENDED) {
                    gDPSetCombineMode(g++, CC_TEX_PRIM, CC_TEX_PRIM);
                } else {
                    gDPSetCombineMode(g++, CC_TEX_SHADE, CC_TEX_SHADE);
                }
            }
        }
        u32 prim = pass == PASS_HULL ? hull_rgba : b->rgba;
        if (pass != PASS_OPAQUE || want == HC_FP_NO_TEXTURE)
            gDPSetPrimColor(g++, 0, 0, HC_R(prim), HC_G(prim), HC_B(prim), pass == PASS_OPAQUE ? 255 : HC_A(prim));

        Vtx* vb = &s.pool[s.pool_flip][s.pool_used];
        s.pool_used += b->vertex_count;
        const uint16_t* idx = hc_fp_batch_indices(m, b);
        float tw = 0.0f, th = 0.0f;
        if (want != HC_FP_NO_TEXTURE) {
            tw = (float)s.pack.textures[want].width * 32.0f;
            th = (float)s.pack.textures[want].height * 32.0f;
        }
        float push = pass == PASS_HULL ? HULL_PUSH : 0.0f;
        for (int k = 0; k < b->vertex_count; k++) {
            int i = idx[k];
            int at = base + i - v0;
            const float* p = s.pos[at];
            const float* n = s.nrm[at];
            Vtx* o = &vb[k];
            /* Halo model (forward, left, up) -> AC model (left, up, forward). */
            o->v.ob[0] = to_s16((p[1] + n[1] * push) * POS_Q);
            o->v.ob[1] = to_s16((p[2] + n[2] * push) * POS_Q);
            o->v.ob[2] = to_s16((p[0] + n[0] * push) * POS_Q);
            o->v.flag = 0;
            o->v.tc[0] = to_s16((m->vertices[i].uv[0] - b->uv_offset[0]) * tw);
            o->v.tc[1] = to_s16((m->vertices[i].uv[1] - b->uv_offset[1]) * th);
            o->v.cn[0] = s.rgb[at][0];
            o->v.cn[1] = s.rgb[at][1];
            o->v.cn[2] = s.rgb[at][2];
            o->v.cn[3] = 255;
        }
        gSPVertex(g++, vb, b->vertex_count, 0);
        const uint8_t* tri = hc_fp_batch_triangles(m, b);
        int nt = b->triangle_count, t = 0;
        for (; t + 1 < nt; t += 2) {
            const uint8_t* q = tri + t * 3;
            gSP2Triangles(g++, q[0], q[1], q[2], 0, q[3], q[4], q[5], 0);
        }
        if (t < nt) gSP1Triangle(g++, tri[t * 3], tri[t * 3 + 1], tri[t * 3 + 2], 0);
        s.stats.vertices += b->vertex_count;
    }
    return g;
}

static void light_for(GAME_PLAY* play, const HaloUnit* u, float yaw, UnitLight* L) {
    const LightDiffuse* sun = &play->kankyo.sun_light.lights.diffuse;
    const u8* amb = play->global_light.ambientColor;
    /* AC direction (x, y up, z south) -> Halo world -> this unit's model space. */
    float hx = (float)sun->x, hy = -(float)sun->z, hz = (float)sun->y;
    float len = sqrtf(hx * hx + hy * hy + hz * hz);
    if (len < 1e-3f) {
        hx = 0.3f;
        hy = 0.2f;
        hz = 0.9f;
        len = sqrtf(hx * hx + hy * hy + hz * hz);
    }
    hx /= len;
    hy /= len;
    hz /= len;
    float c = cosf(yaw), sn = sinf(yaw);
    L->dir[0] = hx * c + hy * sn;
    L->dir[1] = -hx * sn + hy * c;
    L->dir[2] = hz;
    for (int k = 0; k < 3; k++) {
        L->ambient[k] = (float)amb[k];
        L->sun[k] = (float)sun->color[k];
    }
    L->flash = hc_clampf(u->hurt_flash, 0.0f, 1.0f) * 0.6f;
}

static void pose_unit(const HcFpModel* m, const UnitAnim* a) {
    hc_fp_sample(m, a->cur.anim, a->cur.frame, a->cur.loop, s.local);
    if (a->fade < 1.0f) {
        hc_fp_sample(m, a->prev.anim, a->prev.frame, a->prev.loop, s.from);
        hc_fp_blend(s.from, s.local, a->fade, m->node_count);
        memcpy(s.local, s.from, sizeof(HcFpXf) * (size_t)m->node_count);
    }
    if (a->fire_frame >= 0.0f) hc_fp_overlay(m, a->fire_anim, a->fire_frame, 1.0f, s.local);
    hc_fp_pose(m, s.local, s.world, s.skin);
}

void hc_biped_view_begin_frame(void) {
    s.pool_flip ^= 1;
    s.pool_used = 0;
    memset(&s.stats, 0, sizeof(s.stats));
}

int hc_biped_view_draw(Gfx** opa, Gfx** xlu, GRAPH* graph, GAME_PLAY* play, const HaloSim* sim, int ui,
                       const HcView* v) {
    const HaloUnit* u = &sim->units[ui];
    const HcFpModel* m = model_for(u);
    if (m == NULL) return 0;
    UnitAnim* a = state_for(sim, ui);

    hv3 hpos = halo_unit_pos_interp(u, sim->alpha);
    float yaw = lerp_angle(u->prev_yaw, u->yaw, sim->alpha);
    xyz_t ap = hc_h2a_pos(hpos);
    const xyz_t* eye = &play->view.eye;
    float dx = ap.x - eye->x, dy = ap.y - eye->y, dz = ap.z - eye->z;
    float fx = play->view.center.x - eye->x, fy = play->view.center.y - eye->y, fz = play->view.center.z - eye->z;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl > 1e-3f && (dx * fx + dy * fy + dz * fz) / fl < -CULL_RADIUS_AC) {
        s.stats.culled++;
        return 1;
    }
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    int lod = dist < LOD1_AC ? 0 : (dist < LOD2_AC ? 1 : 2);
    if (lod >= m->lod_count) lod = m->lod_count - 1;
    (void)v;

    pose_unit(m, a);
    UnitLight L;
    light_for(play, u, yaw, &L);
    int nb = skin_range(m, lod, 0, NULL, &L);
    if (nb < 0) return 0;
    const HcFpModel* wm = u->dead ? NULL : weapon_for(u->weapon.id);
    int wlod = 0, nw = 0;
    if (wm && m->hand_node >= 0) {
        HcFpMtx hand, mtx;
        hc_fp_xf_to_mtx(&m->hand, hand);
        hc_fp_mtx_mul(s.world[m->hand_node], hand, mtx);
        wlod = lod < wm->lod_count ? lod : wm->lod_count - 1;
        nw = skin_range(wm, wlod, nb, (const HcFpMtx*)&mtx, &L);
        if (nw < 0) wm = NULL;
    } else {
        wm = NULL;
    }

    float root[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (a->death >= 0 && a->cur.anim == a->death) hc_fp_root(m, a->cur.anim, a->cur.frame, root);
    Matrix_translate(ap.x, ap.y, ap.z, MTX_LOAD);
    Matrix_RotateY(hc_h2a_yaw(yaw), MTX_MULT);
    Matrix_translate(hc_h2a_len(root[1]), hc_h2a_len(root[2]), hc_h2a_len(root[0]), MTX_MULT);
    float k = HALO_TO_AC_SCALE / POS_Q;
    Matrix_scale(k, k, k, MTX_MULT);
    Mtx* mtx = _Matrix_to_Mtx_new(graph);
    if (mtx == NULL) return 0;

    Gfx* g = *opa;
    gSPMatrix(g++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    int tex = -2;
    g = emit(g, m, lod, 0, PASS_OPAQUE, 0, &tex);
    if (wm) g = emit(g, wm, wlod, nb, PASS_OPAQUE, 0, &tex);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    *opa = hc_gfx_mode_opa(g);

    int hull = u->shield_flash > 0.0f && !u->dead;
    g = *xlu;
    gSPMatrix(g++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_ZB_XLU_SURF | G_RM_ZB_XLU_SURF2);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    tex = -2;
    g = emit(g, m, lod, 0, PASS_BLENDED, 0, &tex);
    if (wm) g = emit(g, wm, wlod, nb, PASS_BLENDED, 0, &tex);
    if (hull) {
        u32 rgba = SHIELD_RGB | (u32)(hc_clampf(u->shield_flash, 0.0f, 1.0f) * 140.0f);
        g = emit(g, m, lod, 0, PASS_HULL, rgba, &tex);
    }
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    *xlu = hc_gfx_mode_xlu(g);
    s.stats.drawn++;
    return 1;
}
