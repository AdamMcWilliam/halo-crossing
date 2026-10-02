#include "hc_audio.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hc_sound.h"
#include "pc_settings.h"

#ifndef HC_HALO_ASSET_DIR
#define HC_HALO_ASSET_DIR "assets_local/halo/generated"
#endif

#define OUT_RATE 44100
#define MAX_PENDING 16
#define MAX_BLASTS 6
#define MASTER_TRIM 0.8f      /* Halo is mastered hotter than Animal Crossing */
#define CHATTER_RATE 2.5f     /* non-urgent lines per second, town-wide */
#define ALERT_GAP 0.6f
#define IMPACTS_PER_FRAME 4
#define SHIELD_LOW 0.25f

typedef char check_weapons[HC_SND_WEAPONS == HALO_WEAPON_COUNT ? 1 : -1];

/* Loudness and reach by kind of sound, in wu. */
typedef struct SoundClass {
    float gain, min_dist, max_dist;
    int max_instances;
} SoundClass;

static const SoundClass k_fire = { 0.9f, 1.2f, 45.0f, 4 };
static const SoundClass k_gear = { 0.8f, 0.6f, 10.0f, 1 };
static const SoundClass k_blast = { 1.0f, 3.0f, 70.0f, 4 };
static const SoundClass k_impact = { 0.5f, 0.4f, 14.0f, 4 };
static const SoundClass k_shield = { 0.6f, 0.5f, 16.0f, 3 };
static const SoundClass k_grenade = { 0.7f, 0.5f, 16.0f, 2 };
static const SoundClass k_ui = { 0.55f, 0.0f, 0.0f, 1 };
static const SoundClass k_voice = { 1.0f, 1.2f, 32.0f, 3 };

typedef struct UnitAudio {
    int serial;
    unsigned line, flame;
    float line_cooldown;
} UnitAudio;

typedef struct Pending {
    float t;
    int unit, serial, line, death;
} Pending;

typedef struct Blast {
    hv3 at;
    float radius, age;
} Blast;

int pc_audio_is_active(void);

static struct {
    int tried, ok;
    char status[160];
    HcSoundPack pack;
    HcMixer mix;
    SDL_AudioDeviceID dev;
    int dev_tried;
    float wait_for_host;
    double dump_frames; /* offline mixing: frames owed to the dump file */
    int paused;
    UnitAudio units[HALO_MAX_UNITS];
    Pending pending[MAX_PENDING];
    Blast blasts[MAX_BLASTS];
    int next_blast;
    float chatter, alert_gap, shield_hit_gap;
    int impacts;
    unsigned shield_low, shield_charge;
    uint32_t rng;
    /* HC_AUDIO_DUMP: mix on the game thread into this raw s16 stereo file
     * instead of opening a device (unattended checks). */
    FILE* dump;
    HcAudioStats stats;
} s;

static int live(void) { return s.ok && (s.dev != 0 || s.dump != NULL); }

static float rnd(void) {
    s.rng = s.rng * 1664525u + 1013904223u;
    return (float)((s.rng >> 8) & 0xFFFF) / 65535.0f;
}

static void SDLCALL audio_callback(void* userdata, Uint8* stream, int len) {
    hc_mixer_mix(&s.mix, (int16_t*)stream, len / 4);
}

void hc_audio_init(void) {
    if (s.tried) return;
    s.tried = 1;
    s.rng = 0x5EED1234u;
    const char* dir = getenv("HC_HALO_ASSETS");
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/%s", dir && dir[0] ? dir : HC_HALO_ASSET_DIR, HC_SOUND_PACK_NAME);
    if (!hc_sound_pack_load(&s.pack, path, err, sizeof(err))) {
        snprintf(s.status, sizeof(s.status), "Halo sounds: none (%s)", err);
    } else {
        int n = 0;
        for (int i = 0; i < HC_SND_COUNT; i++) n += hc_sound_has(&s.pack, i);
        hc_mixer_init(&s.mix, OUT_RATE, 0xC0FFEEu);
        s.ok = 1;
        const char* dump = getenv("HC_AUDIO_DUMP");
        if (dump && dump[0]) s.dump = fopen(dump, "wb");
        snprintf(s.status, sizeof(s.status), "Halo sounds: %d of %d%s", n, HC_SND_COUNT, s.dump ? " (to file)" : "");
    }
    printf("[halo-crossing] %s [%s]\n", s.status, path);
}

/* Opened once the host's own audio is up: some SDL drivers (dummy) allow a
 * single output device, and the host paces frames by its audio buffer. */
static void open_device(float dt) {
    if (s.dev_tried || s.dump) return;
    s.wait_for_host += dt;
    if (!pc_audio_is_active() && s.wait_for_host < 3.0f) return;
    s.dev_tried = 1;
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = OUT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    s.dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s.dev == 0) {
        snprintf(s.status, sizeof(s.status), "Halo sounds: no audio device (%s)", SDL_GetError());
        printf("[halo-crossing] %s\n", s.status);
        return;
    }
    SDL_PauseAudioDevice(s.dev, 0);
}

static void mix_to_file(float dt) {
    static int16_t buf[2048 * 2];
    s.dump_frames += (double)dt * OUT_RATE;
    while (s.dump_frames >= 1.0) {
        int n = s.dump_frames < 2048.0 ? (int)s.dump_frames : 2048;
        hc_mixer_mix(&s.mix, buf, n);
        fwrite(buf, 4, (size_t)n, s.dump);
        s.dump_frames -= n;
    }
    fflush(s.dump);
}

const char* hc_audio_status(void) { return s.status; }

const HcAudioStats* hc_audio_stats(void) { return &s.stats; }

static unsigned play(int sound, int positional, hv3 at, const SoundClass* c, float gain) {
    if (!live() || !hc_sound_has(&s.pack, sound)) return 0;
    HcSoundParams sp = { positional && c->max_dist > 0.0f, at.x, at.y, at.z, c->gain * gain,
                         c->min_dist, c->max_dist, 0.0f, c->max_instances };
    SDL_LockAudioDevice(s.dev);
    unsigned h = hc_mixer_play(&s.mix, &s.pack, sound, &sp);
    SDL_UnlockAudioDevice(s.dev);
    if (h) s.stats.played++;
    return h;
}

static unsigned play_at(int sound, hv3 at, const SoundClass* c, float gain) { return play(sound, 1, at, c, gain); }

static void stop(unsigned* h, float fade) {
    if (!live() || !*h) return;
    SDL_LockAudioDevice(s.dev);
    hc_mixer_stop(&s.mix, *h, fade);
    SDL_UnlockAudioDevice(s.dev);
    *h = 0;
}

static int playing(unsigned h) {
    if (!live() || !h) return 0;
    SDL_LockAudioDevice(s.dev);
    int p = hc_mixer_playing(&s.mix, h);
    SDL_UnlockAudioDevice(s.dev);
    return p;
}

static const HaloUnit* unit_at(const HaloSim* sim, int i) {
    return i >= 0 && i < HALO_MAX_UNITS && sim->units[i].active ? &sim->units[i] : NULL;
}

static UnitAudio* unit_audio(const HaloSim* sim, int i) {
    const HaloUnit* u = unit_at(sim, i);
    if (!u) return NULL;
    UnitAudio* a = &s.units[i];
    if (a->serial != u->serial) {
        memset(a, 0, sizeof(*a));
        a->serial = u->serial;
    }
    return a;
}

static int voice_base(const HaloUnit* u) {
    if (u->team != HALO_TEAM_COVENANT) return -1;
    if (u->biped == HALO_BIPED_GRUNT) return HC_SND_GRUNT;
    if (u->biped == HALO_BIPED_ELITE) return HC_SND_ELITE;
    return -1;
}

/* One line at a time per unit. Urgent lines (deaths, a grenade stuck to you)
 * cut whatever the unit was saying and skip the town-wide chatter budget. */
static void say(const HaloSim* sim, int ui, HcSoundLine line, int urgent) {
    const HaloUnit* u = unit_at(sim, ui);
    UnitAudio* a = unit_audio(sim, ui);
    if (!u || !a) return;
    int base = voice_base(u);
    if (base < 0 || !hc_sound_has(&s.pack, base + (int)line)) return;
    if (!urgent) {
        if (u->dead || a->line_cooldown > 0.0f || s.chatter < 1.0f || playing(a->line)) return;
        s.chatter -= 1.0f;
    } else {
        stop(&a->line, 0.04f);
    }
    a->line = play_at(base + (int)line, halo_unit_eye(u), &k_voice, 1.0f);
    if (a->line) {
        a->line_cooldown = 1.2f + rnd();
        s.stats.lines++;
    }
}

static void later(const HaloSim* sim, int ui, HcSoundLine line, float delay, int death) {
    const HaloUnit* u = unit_at(sim, ui);
    if (!u) return;
    for (int i = 0; i < MAX_PENDING; i++) {
        Pending* p = &s.pending[i];
        if (p->t > 0.0f) continue;
        p->t = delay > 0.001f ? delay : 0.001f;
        p->unit = ui;
        p->serial = u->serial;
        p->line = (int)line;
        p->death = death;
        return;
    }
}

/* The nearest living Grunt or Elite to `at`, other than `skip`. */
static int nearest_covenant(const HaloSim* sim, hv3 at, float range, int skip) {
    int best = -1;
    float best_d = range;
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = unit_at(sim, i);
        if (!u || u->dead || i == skip || voice_base(u) < 0) continue;
        float d = hv3_dist(u->pos, at);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int fire_sound(int weapon, float charged) {
    if (weapon == HALO_WEAPON_PLASMA_PISTOL && charged > 0.0f) return HC_SND_FIRE_PLASMA_CHARGED;
    return weapon >= 0 && weapon < HC_SND_WEAPONS ? HC_SND_FIRE + weapon : -1;
}

static void impact(const HaloSim* sim, const HaloEvent* e) {
    if (s.impacts >= IMPACTS_PER_FRAME) return;
    const HaloUnit* hit = unit_at(sim, e->other);
    int proj = e->def, sound = -1;
    float gain = 1.0f;
    int bullet = proj == HALO_PROJ_AR_BULLET || proj == HALO_PROJ_PISTOL_BULLET || proj == HALO_PROJ_SHOTGUN_PELLET ||
                 proj == HALO_PROJ_SNIPER_BULLET;
    if (hit) {
        if (hit->is_player) return; /* the shield/UI sounds cover it */
        if (hit->shield > 0.0f && hit->biped == HALO_BIPED_ELITE) sound = HC_SND_IMPACT_COV_SHIELD;
        else if (proj == HALO_PROJ_NEEDLE) sound = HC_SND_IMPACT_NEEDLE;
        else if (proj != HALO_PROJ_FLAME) sound = HC_SND_IMPACT_FLESH;
    } else if (bullet) {
        sound = rnd() < 0.3f ? HC_SND_IMPACT_RICOCHET : HC_SND_IMPACT_DIRT;
        gain = 0.8f;
    } else if (proj == HALO_PROJ_PLASMA_PISTOL_BOLT || proj == HALO_PROJ_PLASMA_RIFLE_BOLT) {
        sound = HC_SND_IMPACT_PLASMA;
    } else if (proj == HALO_PROJ_PLASMA_PISTOL_CHARGED) {
        sound = HC_SND_IMPACT_PLASMA;
        gain = 1.4f;
    } else if (proj == HALO_PROJ_NEEDLE) {
        sound = HC_SND_IMPACT_NEEDLE;
    }
    if (sound >= 0 && play_at(sound, e->pos, sound == HC_SND_IMPACT_COV_SHIELD ? &k_shield : &k_impact, gain))
        s.impacts++;
}

static void explosion(const HaloEvent* e) {
    int sound = -1;
    switch (e->def) {
        case HALO_PROJ_FRAG_GRENADE:
        case HALO_PROJ_ROCKET: sound = HC_SND_EXPL_FRAG; break;
        case HALO_PROJ_PLASMA_GRENADE: sound = HC_SND_EXPL_PLASMA_GRENADE; break;
        case HALO_PROJ_FUEL_ROD: sound = HC_SND_EXPL_FUEL_ROD; break;
        case HALO_PROJ_NEEDLE: sound = e->value > 0.0f ? HC_SND_EXPL_NEEDLE_SUPER : HC_SND_EXPL_NEEDLE; break;
        default: break;
    }
    if (sound < 0) return;
    play_at(sound, e->pos, sound == HC_SND_EXPL_NEEDLE ? &k_impact : &k_blast, sound == HC_SND_EXPL_NEEDLE ? 1.4f : 1.0f);
    if (e->value > 0.0f) {
        Blast* b = &s.blasts[s.next_blast];
        s.next_blast = (s.next_blast + 1) % MAX_BLASTS;
        b->at = e->pos;
        b->radius = e->value;
        b->age = 0.0f;
    }
}

static int in_blast(hv3 at) {
    for (int i = 0; i < MAX_BLASTS; i++) {
        const Blast* b = &s.blasts[i];
        if (b->radius > 0.0f && b->age < 0.3f && hv3_dist(b->at, at) < b->radius + 0.5f) return 1;
    }
    return 0;
}

void hc_audio_event(const HaloSim* sim, const HaloEvent* e) {
    if (!live()) return;
    const HaloUnit* u = unit_at(sim, e->unit);
    switch (e->type) {
        case HALO_EV_WEAPON_FIRED: {
            int sound = fire_sound(e->def, e->value);
            if (sound < 0) break;
            float gain = u && u->is_player ? 0.85f : 0.75f;
            if (e->def == HALO_WEAPON_FLAMETHROWER) {
                UnitAudio* a = unit_audio(sim, e->unit);
                if (a && !playing(a->flame)) a->flame = play_at(sound, e->pos, &k_fire, gain);
            } else {
                play_at(sound, e->pos, &k_fire, gain);
            }
            break;
        }
        case HALO_EV_RELOAD:
            if (u && u->is_player && e->def >= 0 && e->def < HC_SND_WEAPONS)
                play_at(HC_SND_RELOAD + e->def, e->pos, &k_gear, 1.0f);
            break;
        case HALO_EV_WEAPON_SWITCHED:
            if (u && u->is_player && e->def >= 0 && e->def < HC_SND_WEAPONS)
                play_at(HC_SND_READY + e->def, halo_unit_eye(u), &k_gear, 1.0f);
            break;
        case HALO_EV_PROJECTILE_IMPACT: impact(sim, e); break;
        case HALO_EV_EXPLOSION: explosion(e); break;
        case HALO_EV_UNIT_DAMAGED:
            if (!u) break;
            if (u->is_player) {
                stop(&s.shield_charge, 0.1f);
                if (u->shield > 0.0f && s.shield_hit_gap <= 0.0f) {
                    play(HC_SND_UI_SHIELD_HIT, 0, e->pos, &k_ui, 1.0f);
                    s.shield_hit_gap = 0.25f;
                }
                const HaloUnit* by = unit_at(sim, e->other);
                if (by && voice_base(by) >= 0 && rnd() < 0.12f) later(sim, e->other, HC_LINE_HURT_ENEMY, 0.3f, 0);
            } else if (!u->dead && e->value >= 5.0f && rnd() < 0.6f) {
                say(sim, e->unit, e->value >= 25.0f || u->body < 0.3f ? HC_LINE_PAIN_MAJOR : HC_LINE_PAIN, 0);
            }
            break;
        case HALO_EV_SHIELD_DEPLETED:
            if (u && u->is_player) play(HC_SND_UI_SHIELD_DEPLETED, 0, e->pos, &k_ui, 1.0f);
            else if (u && u->biped == HALO_BIPED_ELITE) play_at(HC_SND_COV_SHIELD_DEPLETE, halo_unit_eye(u), &k_shield, 1.0f);
            break;
        case HALO_EV_SHIELD_RECHARGE:
            if (u && u->is_player && !playing(s.shield_charge))
                s.shield_charge = play(HC_SND_UI_SHIELD_CHARGE, 0, e->pos, &k_ui, 0.8f);
            break;
        case HALO_EV_UNIT_KILLED:
            if (!u) break;
            if (u->is_player) {
                int c = nearest_covenant(sim, u->pos, 25.0f, -1);
                if (c >= 0) later(sim, c, HC_LINE_CELEBRATE, 1.0f + rnd() * 0.5f, 0);
            } else if (voice_base(u) >= 0) {
                later(sim, e->unit, HC_LINE_DEATH, 0.0f, 1);
                int f = nearest_covenant(sim, u->pos, 10.0f, e->unit);
                if (f >= 0 && rnd() < 0.4f) later(sim, f, HC_LINE_FRIEND_DIED, 0.7f + rnd() * 0.6f, 0);
            }
            break;
        case HALO_EV_GRENADE_THROWN:
            play_at(HC_SND_GRENADE_THROW, e->pos, &k_grenade, 1.0f);
            if (u && voice_base(u) >= 0) {
                if (rnd() < 0.8f) say(sim, e->unit, HC_LINE_GRENADE_THROW, 0);
            } else if (u && u->is_player) {
                int c = nearest_covenant(sim, u->pos, 12.0f, -1);
                if (c >= 0 && rnd() < 0.5f) later(sim, c, HC_LINE_GRENADE_SIGHTED, 0.4f, 0);
            }
            break;
        case HALO_EV_GRENADE_STUCK:
            play_at(HC_SND_GRENADE_STICK, e->pos, &k_grenade, 1.0f);
            if (unit_at(sim, e->other)) say(sim, e->other, HC_LINE_GRENADE_STUCK, 1);
            break;
        case HALO_EV_OVERHEAT:
            if (u) play_at(HC_SND_OVERHEAT, halo_unit_eye(u), &k_gear, 1.0f);
            break;
        case HALO_EV_AI_ALERTED:
            if (s.alert_gap <= 0.0f && rnd() < 0.7f) {
                say(sim, e->unit, HC_LINE_ALERT, 0);
                s.alert_gap = ALERT_GAP;
            }
            break;
        case HALO_EV_AI_PANIC: say(sim, e->unit, HC_LINE_PANIC, 0); break;
        default: break;
    }
}

static void run_pending(const HaloSim* sim, float dt) {
    for (int i = 0; i < MAX_PENDING; i++) {
        Pending* p = &s.pending[i];
        if (p->t <= 0.0f || (p->t -= dt) > 0.0f) continue;
        p->t = 0.0f;
        const HaloUnit* u = unit_at(sim, p->unit);
        if (!u || u->serial != p->serial) continue;
        if (p->death) {
            HcSoundLine line = in_blast(u->pos) ? HC_LINE_DEATH_FLYING
                               : rnd() < 0.6f   ? HC_LINE_DEATH_VIOLENT
                                                : HC_LINE_DEATH;
            say(sim, p->unit, line, 1);
        } else {
            say(sim, p->unit, (HcSoundLine)p->line, 0);
        }
    }
}

void hc_audio_update(const HaloSim* sim, float dt, int paused) {
    if (!s.ok) return;
    open_device(dt);
    if (!live()) return;
    if (paused != s.paused && s.dev) {
        s.paused = paused;
        SDL_PauseAudioDevice(s.dev, paused);
    }
    s.impacts = 0;
    s.chatter = fminf(s.chatter + CHATTER_RATE * dt, 3.0f);
    s.alert_gap -= dt;
    s.shield_hit_gap -= dt;
    for (int i = 0; i < MAX_BLASTS; i++) s.blasts[i].age += dt;
    run_pending(sim, dt);

    const HaloUnit* p = sim->player >= 0 ? unit_at(sim, sim->player) : NULL;
    float volume = (float)g_pc_settings.master_volume / 100.0f;
    volume = MASTER_TRIM * (volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume);

    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        UnitAudio* a = &s.units[i];
        if (a->line_cooldown > 0.0f) a->line_cooldown -= dt;
        if (!a->flame) continue;
        const HaloUnit* u = unit_at(sim, i);
        if (!u || u->serial != a->serial || u->dead || u->weapon.id != HALO_WEAPON_FLAMETHROWER || !u->control.fire ||
            u->weapon.overheated) {
            stop(&a->flame, 0.15f);
        } else {
            hv3 at = halo_unit_eye(u);
            SDL_LockAudioDevice(s.dev);
            hc_mixer_move(&s.mix, a->flame, at.x, at.y, at.z);
            SDL_UnlockAudioDevice(s.dev);
        }
    }

    int low = p && !p->dead && !sim->infinite_shields && p->shield < SHIELD_LOW;
    if (low && !playing(s.shield_low)) s.shield_low = play(HC_SND_UI_SHIELD_LOW, 0, hv3_make(0, 0, 0), &k_ui, 0.7f);
    else if (!low) stop(&s.shield_low, 0.1f);

    SDL_LockAudioDevice(s.dev);
    s.mix.volume = volume;
    if (p) {
        hv3 ear = halo_unit_eye(p);
        hc_mixer_listener(&s.mix, ear.x, ear.y, ear.z, p->yaw);
    }
    s.stats.voices = hc_mixer_active(&s.mix);
    SDL_UnlockAudioDevice(s.dev);
    if (s.dump && !paused) mix_to_file(dt);
}

void hc_audio_reset(void) {
    if (!live()) return;
    SDL_LockAudioDevice(s.dev);
    for (int i = 0; i < HC_MIX_VOICES; i++)
        if (s.mix.voices[i].samples) hc_mixer_stop(&s.mix, s.mix.voices[i].handle, 0.2f);
    SDL_UnlockAudioDevice(s.dev);
    memset(s.units, 0, sizeof(s.units));
    memset(s.pending, 0, sizeof(s.pending));
    memset(s.blasts, 0, sizeof(s.blasts));
    s.shield_low = s.shield_charge = 0;
}
