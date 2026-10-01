#include "hc_gfx.h"

#include <math.h>
#include <string.h>

#include "m_rcp.h"
#include "sys_matrix.h"
#include "THA_GA.h"

#define HC_OPA_WORDS 32768
#define HC_XLU_WORDS 16384
#define HC_FONT_WORDS 8192
#define HC_GFX_RESERVE 4 /* room for the closing gSPEndDisplayList */

/* Two of each so a buffer is never rewritten while a late frame reads it. */
static Gfx s_opa[2][HC_OPA_WORDS];
static Gfx s_xlu[2][HC_XLU_WORDS];
static Gfx s_font[2][HC_FONT_WORDS];
static int s_flip_opa, s_flip_xlu, s_flip_font;

static Vtx s_cube[24];
static Vtx s_glow[9];
static Mtx s_identity;
static int s_inited;

static HcGfxStats s_stats;

HcGfxStats* hc_gfx_stats(void) {
    return &s_stats;
}

static void set_vtx(Vtx* v, int x, int y, int z, u8 r, u8 g, u8 b, u8 a) {
    v->v.ob[0] = (s16)x;
    v->v.ob[1] = (s16)y;
    v->v.ob[2] = (s16)z;
    v->v.flag = 0;
    v->v.tc[0] = 0;
    v->v.tc[1] = 0;
    v->v.cn[0] = r;
    v->v.cn[1] = g;
    v->v.cn[2] = b;
    v->v.cn[3] = a;
}

void hc_gfx_init(void) {
    if (s_inited) return;
    s_inited = 1;

    /* Unit cube (+-100) with baked per-face light: top bright, bottom dark. */
    static const u8 shade[6] = { 205, 185, 255, 110, 230, 160 }; /* +x -x +y -y +z -z */
    static const int quad[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int f = 0; f < 6; f++) {
        int axis = f / 2;
        int sign = (f % 2 == 0) ? 1 : -1;
        for (int c = 0; c < 4; c++) {
            int p[3];
            p[axis] = sign * 100;
            p[(axis + 1) % 3] = quad[c][0] * 100;
            p[(axis + 2) % 3] = quad[c][1] * 100;
            set_vtx(&s_cube[f * 4 + c], p[0], p[1], p[2], shade[f], shade[f], shade[f], 255);
        }
    }

    set_vtx(&s_glow[0], 0, 0, 0, 255, 255, 255, 255);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * (6.2831853f / 8.0f);
        set_vtx(&s_glow[1 + i], (int)(cosf(a) * 100.0f), (int)(sinf(a) * 100.0f), 0, 255, 255, 255, 0);
    }

    MtxF id;
    memset(&id, 0, sizeof(id));
    id.mf[0][0] = id.mf[1][1] = id.mf[2][2] = id.mf[3][3] = 1.0f;
    _MtxF_to_Mtx(&id, &s_identity);
}

static void redirect(THA_GA* t, THA_GA* saved, Gfx* buf, size_t words, Gfx** start) {
    *saved = *t;
    THA_GA_ct(t, buf, words * sizeof(Gfx));
    *start = buf;
}

static void restore(THA_GA* t, const THA_GA* saved, Gfx* start, int* used) {
    Gfx* head = t->thaGfx.head_p;
    Gfx* tail = t->thaGfx.tail_p;
    int ok = (tail - head) >= HC_GFX_RESERVE;
    *used = (int)(head - start);
    if (ok) {
        gSPEndDisplayList(head++);
    } else {
        s_stats.overflows++;
    }
    *t = *saved;
    if (ok && head - start > 1) gSPDisplayList(t->thaGfx.head_p++, start);
}

void hc_gfx_begin(GRAPH* graph, HcGfxRedirect* r, int flags) {
    hc_gfx_init();
    r->flags = flags;
    if (flags & HC_GFX_OPA) {
        s_flip_opa ^= 1;
        redirect(&graph->polygon_opaque_thaga, &r->saved_opa, s_opa[s_flip_opa], HC_OPA_WORDS, &r->opa_start);
    }
    if (flags & HC_GFX_XLU) {
        s_flip_xlu ^= 1;
        redirect(&graph->polygon_translucent_thaga, &r->saved_xlu, s_xlu[s_flip_xlu], HC_XLU_WORDS, &r->xlu_start);
    }
    if (flags & HC_GFX_FONT) {
        s_flip_font ^= 1;
        redirect(&graph->font_thaga, &r->saved_font, s_font[s_flip_font], HC_FONT_WORDS, &r->font_start);
    }
}

void hc_gfx_end(GRAPH* graph, HcGfxRedirect* r) {
    if (r->flags & HC_GFX_FONT) restore(&graph->font_thaga, &r->saved_font, r->font_start, &s_stats.font_used);
    if (r->flags & HC_GFX_XLU)
        restore(&graph->polygon_translucent_thaga, &r->saved_xlu, r->xlu_start, &s_stats.xlu_used);
    if (r->flags & HC_GFX_OPA)
        restore(&graph->polygon_opaque_thaga, &r->saved_opa, r->opa_start, &s_stats.opa_used);
    r->flags = 0;
}

u32 hc_rgba_lerp(u32 a, u32 b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    int r = (int)HC_R(a) + (int)(((int)HC_R(b) - (int)HC_R(a)) * t);
    int g = (int)HC_G(a) + (int)(((int)HC_G(b) - (int)HC_G(a)) * t);
    int bl = (int)HC_B(a) + (int)(((int)HC_B(b) - (int)HC_B(a)) * t);
    int al = (int)HC_A(a) + (int)(((int)HC_A(b) - (int)HC_A(a)) * t);
    return HC_RGBA(r, g, bl, al);
}

#define HC_CC_SHADE_PRIM SHADE, 0, PRIMITIVE, 0, SHADE, 0, PRIMITIVE, 0

Gfx* hc_gfx_mode_opa(Gfx* g) {
    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_AA_ZB_OPA_SURF | G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(g++, HC_CC_SHADE_PRIM, HC_CC_SHADE_PRIM);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    return g;
}

Gfx* hc_gfx_mode_xlu(Gfx* g) {
    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_ZB_XLU_SURF | G_RM_ZB_XLU_SURF2);
    gDPSetCombineMode(g++, HC_CC_SHADE_PRIM, HC_CC_SHADE_PRIM);
    gSPTexture(g++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gSPLoadGeometryMode(g++, G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH);
    return g;
}

Gfx* hc_gfx_box(Gfx* g, GRAPH* graph, float cx, float cy, float cz, float hx, float hy, float hz, u32 rgba) {
    Matrix_push();
    Matrix_translate(cx, cy, cz, MTX_MULT);
    Matrix_scale(hx * 0.01f, hy * 0.01f, hz * 0.01f, MTX_MULT);
    Mtx* m = _Matrix_to_Mtx_new(graph);
    Matrix_pull();
    if (m == NULL) return g;
    gDPSetPrimColor(g++, 0, 0, HC_R(rgba), HC_G(rgba), HC_B(rgba), HC_A(rgba));
    gSPMatrix(g++, m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(g++, s_cube, 24, 0);
    for (int f = 0; f < 6; f++) {
        int b = f * 4;
        gSP2Triangles(g++, b + 0, b + 1, b + 2, 0, b + 0, b + 2, b + 3, 0);
    }
    return g;
}

Gfx* hc_gfx_glow(Gfx* g, GRAPH* graph, GAME_PLAY* play, xyz_t pos, float radius, u32 rgba) {
    Matrix_translate(pos.x, pos.y, pos.z, MTX_LOAD);
    Matrix_mult(&play->billboard_matrix, MTX_MULT);
    Matrix_scale(radius * 0.01f, radius * 0.01f, radius * 0.01f, MTX_MULT);
    Mtx* m = _Matrix_to_Mtx_new(graph);
    if (m == NULL) return g;
    gDPSetPrimColor(g++, 0, 0, HC_R(rgba), HC_G(rgba), HC_B(rgba), HC_A(rgba));
    gSPMatrix(g++, m, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(g++, s_glow, 9, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
    gSP2Triangles(g++, 0, 3, 4, 0, 0, 4, 5, 0);
    gSP2Triangles(g++, 0, 5, 6, 0, 0, 6, 7, 0);
    gSP2Triangles(g++, 0, 7, 8, 0, 0, 8, 1, 0);
    return g;
}

Gfx* hc_gfx_beam(Gfx* g, GRAPH* graph, xyz_t a, xyz_t b, xyz_t eye, float width, u32 ca, u32 cb) {
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float ex = eye.x - a.x, ey = eye.y - a.y, ez = eye.z - a.z;
    /* side = normalize(dir x to_eye) */
    float sx = dy * ez - dz * ey;
    float sy = dz * ex - dx * ez;
    float sz = dx * ey - dy * ex;
    float sl = sqrtf(sx * sx + sy * sy + sz * sz);
    if (sl < 1e-4f) return g;
    float k = width * 0.5f / sl;
    sx *= k;
    sy *= k;
    sz *= k;
    Vtx* v = GRAPH_ALLOC_TYPE(graph, Vtx, 4);
    if (v == NULL) return g;
    set_vtx(&v[0], (int)(a.x + sx), (int)(a.y + sy), (int)(a.z + sz), 255, 255, 255, HC_A(ca));
    set_vtx(&v[1], (int)(a.x - sx), (int)(a.y - sy), (int)(a.z - sz), 255, 255, 255, HC_A(ca));
    set_vtx(&v[2], (int)(b.x - sx), (int)(b.y - sy), (int)(b.z - sz), 255, 255, 255, HC_A(cb));
    set_vtx(&v[3], (int)(b.x + sx), (int)(b.y + sy), (int)(b.z + sz), 255, 255, 255, HC_A(cb));
    gDPSetPrimColor(g++, 0, 0, HC_R(cb), HC_G(cb), HC_B(cb), 255);
    gSPMatrix(g++, &s_identity, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(g++, v, 4, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
    return g;
}

Gfx* hc_gfx_hud_mode(Gfx* g) {
    gDPPipeSync(g++);
    gDPSetOtherMode(g++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    return g;
}

Gfx* hc_gfx_hud_rect(Gfx* g, float x, float y, float w, float h, u32 rgba) {
    if (w <= 0.0f || h <= 0.0f || HC_A(rgba) == 0) return g;
    int xl = (int)(x * 4.0f), yl = (int)(y * 4.0f);
    int xh = (int)((x + w) * 4.0f), yh = (int)((y + h) * 4.0f);
    if (xh <= xl) xh = xl + 1;
    if (yh <= yl) yh = yl + 1;
    gDPSetPrimColor(g++, 0, 0, HC_R(rgba), HC_G(rgba), HC_B(rgba), HC_A(rgba));
    g = gfx_gSPTextureRectangle1(g, xl, yl, xh, yh, 0, 0, 0, 0, 0);
    return g;
}
