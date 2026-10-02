/* Halo sounds: the pack written by tools/halo_import/sound_pack.py and a
 * small 3D mixer (no SDL; hc_audio.c owns the device). Positions are Halo
 * world units: x forward at yaw 0, y left, z up. */
#ifndef HC_SOUND_H
#define HC_SOUND_H

#include <stddef.h>
#include <stdint.h>

#define HC_SOUND_PACK_NAME "sounds.hcpk"
#define HC_SOUND_RATE 22050
#define HC_SND_WEAPONS 10 /* HALO_WEAPON_COUNT */

/* Covenant dialogue, per character (LINES in sound_pack.py). */
typedef enum HcSoundLine {
    HC_LINE_ALERT,
    HC_LINE_PANIC,
    HC_LINE_PAIN,
    HC_LINE_PAIN_MAJOR,
    HC_LINE_DEATH,
    HC_LINE_DEATH_VIOLENT,
    HC_LINE_DEATH_FLYING,
    HC_LINE_GRENADE_THROW,
    HC_LINE_GRENADE_STUCK,
    HC_LINE_GRENADE_SIGHTED,
    HC_LINE_FRIEND_DIED,
    HC_LINE_CELEBRATE,
    HC_LINE_HURT_ENEMY,
    HC_LINE_DIVE,
    HC_LINE_COUNT
} HcSoundLine;

/* Pack slots, in SOUNDS order. */
typedef enum HcSoundId {
    HC_SND_FIRE = 0, /* + HaloWeaponId */
    HC_SND_READY = HC_SND_FIRE + HC_SND_WEAPONS,
    HC_SND_RELOAD = HC_SND_READY + HC_SND_WEAPONS,
    HC_SND_FIRE_PLASMA_CHARGED = HC_SND_RELOAD + HC_SND_WEAPONS,
    HC_SND_EXPL_FRAG,
    HC_SND_EXPL_PLASMA_GRENADE,
    HC_SND_EXPL_FUEL_ROD,
    HC_SND_EXPL_NEEDLE,
    HC_SND_EXPL_NEEDLE_SUPER,
    HC_SND_IMPACT_RICOCHET,
    HC_SND_IMPACT_DIRT,
    HC_SND_IMPACT_PLASMA,
    HC_SND_IMPACT_NEEDLE,
    HC_SND_IMPACT_FLESH,
    HC_SND_IMPACT_SHIELD,
    HC_SND_IMPACT_COV_SHIELD,
    HC_SND_COV_SHIELD_DEPLETE,
    HC_SND_GRENADE_THROW,
    HC_SND_GRENADE_BOUNCE,
    HC_SND_GRENADE_STICK,
    HC_SND_OVERHEAT,
    HC_SND_UI_SHIELD_HIT,
    HC_SND_UI_SHIELD_DEPLETED,
    HC_SND_UI_SHIELD_CHARGE,
    HC_SND_UI_SHIELD_LOW,
    HC_SND_GRUNT, /* + HcSoundLine */
    HC_SND_ELITE = HC_SND_GRUNT + HC_LINE_COUNT,
    HC_SND_COUNT = HC_SND_ELITE + HC_LINE_COUNT
} HcSoundId;

typedef struct HcSoundPerm {
    uint32_t offset, frames;
    float gain;
    uint32_t pad;
} HcSoundPerm;

typedef struct HcSoundClip {
    int perm_count;
    const HcSoundPerm* perms;
    float pitch_lo, pitch_hi;
} HcSoundClip;

typedef struct HcSoundPack {
    void* data;
    size_t size;
    int loaded;
    HcSoundClip clips[HC_SND_COUNT];
} HcSoundPack;

int hc_sound_pack_load(HcSoundPack* p, const char* path, char* err, size_t err_size);
void hc_sound_pack_free(HcSoundPack* p);
int hc_sound_has(const HcSoundPack* p, int sound);
const int16_t* hc_sound_samples(const HcSoundPack* p, int sound, int perm, uint32_t* frames);

#define HC_MIX_VOICES 48

typedef struct HcSoundParams {
    int positional;
    float x, y, z;
    float gain;
    float min_dist, max_dist; /* full volume inside min, silent past max */
    float pitch;              /* 0 = 1 */
    int max_instances;        /* of this sound at once (0 = no limit); the oldest is cut */
} HcSoundParams;

typedef struct HcVoice {
    const int16_t* samples;
    uint32_t frames;
    double pos;
    float step;
    float gain_l, gain_r;     /* what the last buffer ended at */
    float target_l, target_r; /* from the listener */
    float fade;               /* > 0: fading out, per-frame decrement of `level` */
    float level;
    HcSoundParams params;
    int sound;
    unsigned handle;
    unsigned started;
} HcVoice;

typedef struct HcMixer {
    HcVoice voices[HC_MIX_VOICES];
    float ear[3], yaw;
    float volume;   /* master, 0..1 */
    float limiter;  /* gain reduction that keeps loud mixes from clipping */
    int out_rate;
    unsigned next_handle, clock;
    uint32_t rng;
    unsigned char last_perm[HC_SND_COUNT];
} HcMixer;

void hc_mixer_init(HcMixer* m, int out_rate, uint32_t seed);
/* Returns a handle, or 0 if the sound is missing or inaudible from here. */
unsigned hc_mixer_play(HcMixer* m, const HcSoundPack* p, int sound, const HcSoundParams* sp);
int hc_mixer_playing(const HcMixer* m, unsigned handle);
void hc_mixer_stop(HcMixer* m, unsigned handle, float fade_seconds);
void hc_mixer_move(HcMixer* m, unsigned handle, float x, float y, float z);
void hc_mixer_listener(HcMixer* m, float x, float y, float z, float yaw);
/* Interleaved stereo s16; overwrites `out`. */
void hc_mixer_mix(HcMixer* m, int16_t* out, int frames);
int hc_mixer_active(const HcMixer* m);
/* Left/right gain a source at (x, y, z) would get (tests, culling). */
void hc_mixer_spatial(const HcMixer* m, const HcSoundParams* sp, float* l, float* r);

#endif
