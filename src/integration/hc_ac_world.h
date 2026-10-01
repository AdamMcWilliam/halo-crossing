/* NavigationAdapter + AnimalCrossingCollisionAdapter: implements the Halo
 * sandbox's HaloWorldApi on top of Animal Crossing's background collision. */
#ifndef HC_AC_WORLD_H
#define HC_AC_WORLD_H

#include "halo/halo_world.h"

const HaloWorldApi* hc_ac_world_api(void);

/* Drop cached walkability (scene change, trees cut, houses placed). */
void hc_ac_world_invalidate(void);

/* True when `p` (Halo coords) is open, dry, item-free ground a biped can
 * stand on and walk away from; snaps p->z to the ground. */
int hc_ac_world_spawn_ok(hv3* p);

/* Debug counters for the overlay. */
typedef struct HcAcWorldStats {
    int rays;
    int line_checks;
    int paths;
    int path_expansions;
} HcAcWorldStats;

HcAcWorldStats* hc_ac_world_stats(void);

#endif
