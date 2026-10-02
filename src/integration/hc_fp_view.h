/* The Chief's arms and weapon from the user's imported Halo data
 * (hc_fp_pack.h), animated from the player's weapon state. Weapons without
 * imported data keep the placeholder box viewmodel. */
#ifndef HC_FP_VIEW_H
#define HC_FP_VIEW_H

#include "graph.h"
#include "halo/halo_sim.h"
#include "hc_draw.h"

/* Loads assets_local/halo/generated/fp_weapons.hcpk once ($HC_HALO_ASSETS
 * overrides the directory). Safe to call repeatedly. */
void hc_fp_view_init(void);
const char* hc_fp_view_status(void);
int hc_fp_view_available(HaloWeaponId id);

void hc_fp_view_event(const HaloSim* sim, const HaloEvent* e);
void hc_fp_view_update(const HaloUnit* player, float dt);

/* Camera-relative model scale: small enough that it never pokes into walls,
 * large enough that the closest visible vertex stays past the near plane. */
#define HC_FP_VIEW_SCALE 0.25f

/* opa: the opaque list (skins the mesh); then xlu for blended parts. */
Gfx* hc_fp_view_draw_opa(Gfx* g, GRAPH* graph, GAME_PLAY* play, const HcView* v, const HaloUnit* p);
Gfx* hc_fp_view_draw_xlu(Gfx* g, GRAPH* graph, const HcView* v, const HaloUnit* p);

/* Muzzle in first-person space (wu: forward, left, up; unscaled). */
int hc_fp_view_muzzle(const HaloUnit* p, float out[3]);

#endif
