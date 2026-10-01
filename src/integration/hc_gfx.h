/* Low-level drawing for the Halo layer, emitted as AC display lists so it
 * shares the host's projection, depth buffer and fog.
 *
 * The host's per-frame display list arenas are sized for Animal Crossing
 * alone, so Halo geometry is built in private arenas: hc_gfx_begin() points
 * the host's THA_GA heads at our buffers (GRAPH_ALLOC included), and
 * hc_gfx_end() restores them and branches into ours with one gSPDisplayList. */
#ifndef HC_GFX_H
#define HC_GFX_H

#include "types.h"
#include "graph.h"
#include "m_play.h"
#include "libforest/gbi_extensions.h"

enum {
    HC_GFX_OPA = 1 << 0,
    HC_GFX_XLU = 1 << 1,
    HC_GFX_FONT = 1 << 2,
};

typedef struct HcGfxRedirect {
    int flags;
    THA_GA saved_opa;
    THA_GA saved_xlu;
    THA_GA saved_font;
    Gfx* opa_start;
    Gfx* xlu_start;
    Gfx* font_start;
} HcGfxRedirect;

typedef struct HcGfxStats {
    int opa_used, xlu_used, font_used; /* Gfx words last frame */
    int overflows;
} HcGfxStats;

void hc_gfx_init(void);
void hc_gfx_begin(GRAPH* graph, HcGfxRedirect* r, int flags);
void hc_gfx_end(GRAPH* graph, HcGfxRedirect* r);
HcGfxStats* hc_gfx_stats(void);

#define HC_RGBA(r, g, b, a) (((u32)(r) << 24) | ((u32)(g) << 16) | ((u32)(b) << 8) | (u32)(a))
#define HC_R(c) (((c) >> 24) & 0xFF)
#define HC_G(c) (((c) >> 16) & 0xFF)
#define HC_B(c) (((c) >> 8) & 0xFF)
#define HC_A(c) ((c)&0xFF)

u32 hc_rgba_lerp(u32 a, u32 b, float t);

/* Render state for 3D shaded, untextured geometry. */
Gfx* hc_gfx_mode_opa(Gfx* g);
Gfx* hc_gfx_mode_xlu(Gfx* g);

/* Axis-aligned box in the current matrix space (AC units). */
Gfx* hc_gfx_box(Gfx* g, GRAPH* graph, float cx, float cy, float cz, float hx, float hy, float hz, u32 rgba);

/* Soft camera-facing disc (XLU). */
Gfx* hc_gfx_glow(Gfx* g, GRAPH* graph, GAME_PLAY* play, xyz_t pos, float radius, u32 rgba);

/* Camera-facing ribbon from a to b (XLU), color fades a -> b. */
Gfx* hc_gfx_beam(Gfx* g, GRAPH* graph, xyz_t a, xyz_t b, xyz_t eye, float width, u32 rgba_a, u32 rgba_b);

/* 2D in FONT_DISP, 320x240 screen space. */
Gfx* hc_gfx_hud_mode(Gfx* g);
Gfx* hc_gfx_hud_rect(Gfx* g, float x, float y, float w, float h, u32 rgba);

#endif
