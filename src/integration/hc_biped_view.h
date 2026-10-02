/* Grunts and Elites from the user's imported Halo data (hc_biped_pack.h):
 * skinned, animated from sim state (idle, moving, crouching, fleeing,
 * airborne, grenade throws, flinches, deaths, firing) and lit by Animal
 * Crossing's sun and ambient light. Bipeds without imported data keep the
 * placeholder boxes. */
#ifndef HC_BIPED_VIEW_H
#define HC_BIPED_VIEW_H

#include "graph.h"
#include "halo/halo_sim.h"
#include "hc_biped_pack.h"
#include "hc_draw.h"

/* Loads assets_local/halo/generated/bipeds.hcpk once ($HC_HALO_ASSETS
 * overrides the directory). Safe to call repeatedly. */
void hc_biped_view_init(void);
const char* hc_biped_view_status(void);
int hc_biped_view_available(const HaloUnit* u);

void hc_biped_view_event(const HaloSim* sim, const HaloEvent* e);
void hc_biped_view_update(const HaloSim* sim, float dt);

/* Once per frame before the first draw: recycles the vertex pool. */
void hc_biped_view_begin_frame(void);
/* Opaque parts go to *opa, blended parts and the shield flare to *xlu. 0 if
 * the unit has no imported model (draw the boxes instead). */
int hc_biped_view_draw(Gfx** opa, Gfx** xlu, GRAPH* graph, GAME_PLAY* play, const HaloSim* sim, int unit,
                       const HcView* v);

typedef struct HcBipedStats {
    int drawn, culled, vertices, out_of_vertices;
} HcBipedStats;
HcBipedStats* hc_biped_view_stats(void);

#endif
