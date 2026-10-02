/* Makes the Animal Crossing villagers part of the firefight. Every NPC actor
 * in the scene gets a kinematic, neutral hit volume in the Halo sim that
 * follows it. Gunfire, explosions and Covenant nearby make villagers panic
 * and run (the layer takes over their position), then hide; enough damage
 * knocks them flat until they get back up. Scripted NPCs only flinch and
 * complain, so their scripts never see a missing actor; Tom Nook is the
 * exception. */
#ifndef HC_VILLAGERS_H
#define HC_VILLAGERS_H

#include "halo/halo_sim.h"
#include "hc_draw.h"
#include "m_play.h"

typedef enum HcVillagerState {
    HC_VILLAGER_NORMAL,
    HC_VILLAGER_ALERTED,    /* heard something, freezes and looks */
    HC_VILLAGER_PANICKING,  /* running away from the threat */
    HC_VILLAGER_HIDING,     /* cowering in place */
    HC_VILLAGER_DOWN,       /* "killed": lying flat until it recovers */
    HC_VILLAGER_STATE_COUNT
} HcVillagerState;

void hc_villagers_reset(void);

/* Test beds only: treat scripted NPCs as ordinary villagers too. */
void hc_villagers_set_treat_all(int on);

/* Before halo_sim_advance: adopt new NPCs, drop gone ones, move hit volumes. */
void hc_villagers_sync(GAME_PLAY* play, HaloSim* sim, float dt);

/* For every sim event after the advance. */
void hc_villagers_event(HaloSim* sim, const HaloEvent* e);

/* After the advance: drive panicking / downed actors, age speech. */
void hc_villagers_post(GAME_PLAY* play, HaloSim* sim, float dt);

/* Appends speech bubbles to the HUD's world labels. */
void hc_villagers_labels(HcHudText* hud);

/* " villagers N (...)" for the debug overlay. */
const char* hc_villagers_summary(void);

int hc_villagers_count_state(HcVillagerState s);
int hc_villagers_downed_total(void);

/* Tom Nook is the one scripted NPC who can be put down, and he stays down
 * until the scene reloads. */
int hc_villagers_is_nook(int unit);
int hc_villagers_nook_kills(void);
/* Once per kill: nonzero and where he fell. */
int hc_villagers_take_nook_kill(xyz_t* pos);
/* Any of Nook's storefront actors. */
int hc_villagers_is_nook_actor(const ACTOR* a);

/* HC_SCENARIO=debt: what the Chief owes Nook, in Bells (0 = nothing). While
 * it's unpaid Nook follows him around the shop, steaming, and yells about
 * it, adding interest with every outburst. */
void hc_villagers_set_debt(u32 bells);
u32 hc_villagers_debt(void);
/* Nook's greeting for a customer that owes him, as message-window text.
 * Returns its length, or 0 if it didn't fit. */
int hc_villagers_debt_greeting(unsigned char* text, int cap);

#endif
