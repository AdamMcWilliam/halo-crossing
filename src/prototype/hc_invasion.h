/* Covenant invasion: scatters Grunt / Elite squads over the open ground of
 * the whole Animal Crossing town. Far-off squads stay dormant in the sim
 * until the player gets close or makes noise. */
#ifndef HC_INVASION_H
#define HC_INVASION_H

#include "halo/halo_sim.h"

#define HC_INVASION_SQUADS 14
#define HC_INVASION_MAX_COVENANT 60

/* Spawns up to `squads` squads, none within `keep_clear` wu of `avoid`.
 * Returns the number of Covenant spawned. */
int hc_invasion_populate(HaloSim* sim, hv3 avoid, float keep_clear, int squads);

/* Removes every Covenant unit, living or dead. Returns how many. */
int hc_invasion_clear(HaloSim* sim);

#endif
