/* Halo CE's own HUD drawn from the imported HUD pack (hc_hud_pack.h): shield
 * and health meters, weapon panels and warnings, reticles, grenades, the
 * motion sensor, damage arcs and scope masks. Whatever the pack lacks is
 * left to hc_draw's placeholder HUD. */
#ifndef HC_HUD_VIEW_H
#define HC_HUD_VIEW_H

#include "hc_draw.h"
#include "hc_gfx.h"

enum {
    HC_HUD_DREW_UNIT = 1 << 0,     /* shield and health */
    HC_HUD_DREW_WEAPON = 1 << 1,   /* the ammo, heat or battery panel and its numbers */
    HC_HUD_DREW_WARNINGS = 1 << 2, /* reload, low / no ammo, low / no battery */
    HC_HUD_DREW_RETICLE = 1 << 3,
    HC_HUD_DREW_GRENADES = 1 << 4,
    HC_HUD_DREW_SENSOR = 1 << 5,
    HC_HUD_DREW_DAMAGE = 1 << 6,
    HC_HUD_DREW_SCOPE = 1 << 7,
};

/* Loads assets_local/halo/generated/hud.hcpk once ($HC_HALO_ASSETS
 * overrides the directory). Safe to call repeatedly. */
void hc_hud_view_init(void);
const char* hc_hud_view_status(void);

/* The living first-person player's HUD into FONT_DISP (320x240), leaving
 * hc_gfx_hud_mode set. Returns the HC_HUD_DREW_* parts it drew. */
Gfx* hc_hud_view_draw(Gfx* g, HaloSim* sim, const HaloUnit* p, const HcView* v, int on_enemy, unsigned* drawn);

#endif
