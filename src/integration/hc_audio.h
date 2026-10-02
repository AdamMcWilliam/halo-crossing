/* Halo sound for sim events: weapons, impacts, explosions, shields and
 * Covenant chatter from sounds.hcpk, mixed in 3D around the player on an
 * SDL audio device of its own (the host's audio thread is untouched). */
#ifndef HC_AUDIO_H
#define HC_AUDIO_H

#include "halo/halo_sim.h"

/* Loads assets_local/halo/generated/sounds.hcpk once ($HC_HALO_ASSETS
 * overrides the directory) and opens the device. Silent without the pack. */
void hc_audio_init(void);
const char* hc_audio_status(void);
void hc_audio_event(const HaloSim* sim, const HaloEvent* e);
/* After the frame's events: moves the listener to the player, runs delayed
 * lines and looping sounds. `paused` holds every voice. */
void hc_audio_update(const HaloSim* sim, float dt, int paused);
/* The sim is rebuilt per scene; forget per-unit state. */
void hc_audio_reset(void);

typedef struct HcAudioStats {
    int voices;
    unsigned played, lines;
} HcAudioStats;
const HcAudioStats* hc_audio_stats(void);

#endif
