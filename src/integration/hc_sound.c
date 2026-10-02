#include "hc_sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_VERSION 1
#define MIX_CHUNK 1024

typedef struct PackHeader {
    char magic[4];
    uint32_t version, slot_count, slots_offset;
} PackHeader;

typedef struct PackSlot {
    uint16_t perm_count, pad;
    uint32_t perms_offset;
    float pitch_lo, pitch_hi;
} PackSlot;

typedef char check_header[sizeof(PackHeader) == 16 ? 1 : -1];
typedef char check_slot[sizeof(PackSlot) == 16 ? 1 : -1];
typedef char check_perm[sizeof(HcSoundPerm) == 16 ? 1 : -1];

static int fail(char* err, size_t n, const char* msg, int slot) {
    if (err && n) {
        if (slot >= 0) snprintf(err, n, "sound %d: %s", slot, msg);
        else snprintf(err, n, "%s", msg);
    }
    return 0;
}

static int in_range(const HcSoundPack* p, uint32_t off, size_t bytes) {
    return off <= p->size && bytes <= p->size - off;
}

int hc_sound_pack_load(HcSoundPack* p, const char* path, char* err, size_t n) {
    memset(p, 0, sizeof(*p));
    FILE* f = fopen(path, "rb");
    if (f == NULL) return fail(err, n, "not found", -1);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)sizeof(PackHeader)) {
        fclose(f);
        return fail(err, n, "truncated", -1);
    }
    p->data = malloc((size_t)size);
    p->size = (size_t)size;
    if (p->data == NULL) {
        fclose(f);
        return fail(err, n, "out of memory", -1);
    }
    size_t got = fread(p->data, 1, p->size, f);
    fclose(f);
    const uint8_t* d = (const uint8_t*)p->data;
    const PackHeader* h = (const PackHeader*)d;
    int ok = 0;
    if (got != p->size) fail(err, n, "read error", -1);
    else if (memcmp(h->magic, "HCSD", 4) != 0 || h->version != PACK_VERSION) fail(err, n, "not a v1 sound pack", -1);
    else if (h->slot_count != HC_SND_COUNT) fail(err, n, "slot count differs from this build (re-run the importer)", -1);
    else if ((h->slots_offset & 3) || !in_range(p, h->slots_offset, HC_SND_COUNT * sizeof(PackSlot)))
        fail(err, n, "slot table out of range", -1);
    else ok = 1;
    for (int i = 0; ok && i < HC_SND_COUNT; i++) {
        const PackSlot* s = (const PackSlot*)(d + h->slots_offset) + i;
        HcSoundClip* c = &p->clips[i];
        if (s->perm_count == 0) continue;
        if ((s->perms_offset & 3) || !in_range(p, s->perms_offset, s->perm_count * sizeof(HcSoundPerm))) {
            ok = fail(err, n, "permutations out of range", i);
            break;
        }
        c->perms = (const HcSoundPerm*)(d + s->perms_offset);
        for (int k = 0; k < s->perm_count; k++) {
            const HcSoundPerm* pm = &c->perms[k];
            if ((pm->offset & 1) || pm->frames == 0 || !in_range(p, pm->offset, (size_t)pm->frames * 2)) {
                ok = fail(err, n, "samples out of range", i);
                break;
            }
        }
        c->perm_count = s->perm_count;
        c->pitch_lo = s->pitch_lo > 0.25f ? s->pitch_lo : 1.0f;
        c->pitch_hi = s->pitch_hi >= c->pitch_lo ? s->pitch_hi : c->pitch_lo;
    }
    if (!ok) {
        hc_sound_pack_free(p);
        return 0;
    }
    p->loaded = 1;
    return 1;
}

void hc_sound_pack_free(HcSoundPack* p) {
    free(p->data);
    memset(p, 0, sizeof(*p));
}

int hc_sound_has(const HcSoundPack* p, int sound) {
    return p && p->loaded && sound >= 0 && sound < HC_SND_COUNT && p->clips[sound].perm_count > 0;
}

const int16_t* hc_sound_samples(const HcSoundPack* p, int sound, int perm, uint32_t* frames) {
    if (!hc_sound_has(p, sound) || perm < 0 || perm >= p->clips[sound].perm_count) return NULL;
    const HcSoundPerm* pm = &p->clips[sound].perms[perm];
    if (frames) *frames = pm->frames;
    return (const int16_t*)((const uint8_t*)p->data + pm->offset);
}

/* -- mixer ------------------------------------------------------------- */

static uint32_t next_rand(HcMixer* m) {
    m->rng = m->rng * 1664525u + 1013904223u;
    return m->rng >> 8;
}

static float rand01(HcMixer* m) { return (float)(next_rand(m) & 0xFFFF) / 65535.0f; }

void hc_mixer_init(HcMixer* m, int out_rate, uint32_t seed) {
    memset(m, 0, sizeof(*m));
    m->out_rate = out_rate > 0 ? out_rate : HC_SOUND_RATE;
    m->volume = 1.0f;
    m->limiter = 1.0f;
    m->rng = seed ? seed : 1u;
    memset(m->last_perm, 0xFF, sizeof(m->last_perm));
}

void hc_mixer_spatial(const HcMixer* m, const HcSoundParams* sp, float* l, float* r) {
    float g = sp->gain;
    if (!sp->positional) {
        *l = *r = g;
        return;
    }
    float dx = sp->x - m->ear[0], dy = sp->y - m->ear[1], dz = sp->z - m->ear[2];
    float d = sqrtf(dx * dx + dy * dy + dz * dz);
    float lo = sp->min_dist > 0.01f ? sp->min_dist : 0.01f;
    float hi = sp->max_dist > lo ? sp->max_dist : lo + 1.0f;
    if (d >= hi) {
        *l = *r = 0.0f;
        return;
    }
    if (d > lo) {
        float t = (d - lo) / (hi - lo);
        g *= powf(lo / d, 0.8f) * (1.0f - t * t);
    }
    /* Pan by the bearing in the listener's frame; sources at the ear stay centred. */
    float c = cosf(m->yaw), s = sinf(m->yaw);
    float fwd = dx * c + dy * s, left = -dx * s + dy * c;
    float flat = sqrtf(fwd * fwd + left * left);
    float side = flat > 1e-4f ? left / flat : 0.0f;
    float near = d < 0.6f ? d / 0.6f : 1.0f;
    side *= 0.8f * near;
    if (flat > 1e-4f && fwd < 0.0f) g *= 1.0f + 0.2f * fwd / flat * near;
    *l = g * sqrtf(0.5f * (1.0f + side));
    *r = g * sqrtf(0.5f * (1.0f - side));
}

static HcVoice* find(HcMixer* m, unsigned handle) {
    if (handle == 0) return NULL;
    HcVoice* v = &m->voices[handle % HC_MIX_VOICES];
    return v->samples && v->handle == handle ? v : NULL;
}

int hc_mixer_playing(const HcMixer* m, unsigned handle) { return find((HcMixer*)m, handle) != NULL; }

void hc_mixer_stop(HcMixer* m, unsigned handle, float fade_seconds) {
    HcVoice* v = find(m, handle);
    if (!v) return;
    if (fade_seconds <= 0.0f) v->samples = NULL;
    else if (v->fade <= 0.0f) v->fade = 1.0f / (fade_seconds * (float)m->out_rate);
}

void hc_mixer_move(HcMixer* m, unsigned handle, float x, float y, float z) {
    HcVoice* v = find(m, handle);
    if (!v) return;
    v->params.x = x;
    v->params.y = y;
    v->params.z = z;
    hc_mixer_spatial(m, &v->params, &v->target_l, &v->target_r);
}

int hc_mixer_active(const HcMixer* m) {
    int n = 0;
    for (int i = 0; i < HC_MIX_VOICES; i++) n += m->voices[i].samples != NULL;
    return n;
}

unsigned hc_mixer_play(HcMixer* m, const HcSoundPack* p, int sound, const HcSoundParams* sp) {
    if (!hc_sound_has(p, sound)) return 0;
    float l, r;
    hc_mixer_spatial(m, sp, &l, &r);
    if (l + r < 0.004f) return 0;

    const HcSoundClip* c = &p->clips[sound];
    int perm = (int)(next_rand(m) % (uint32_t)c->perm_count);
    if (c->perm_count > 1 && perm == m->last_perm[sound]) perm = (perm + 1) % c->perm_count;
    m->last_perm[sound] = (unsigned char)perm;

    /* Slot choice: the oldest of this sound past its instance limit, else a
     * free voice, else the quietest. */
    int slot = -1, same = 0, oldest = -1, free_slot = -1, quiet = -1;
    float quiet_gain = 1e9f;
    for (int i = 0; i < HC_MIX_VOICES; i++) {
        HcVoice* v = &m->voices[i];
        if (!v->samples) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (v->sound == sound) {
            same++;
            if (oldest < 0 || v->started < m->voices[oldest].started) oldest = i;
        }
        float g = (v->target_l + v->target_r) * v->level;
        if (g < quiet_gain) {
            quiet_gain = g;
            quiet = i;
        }
    }
    if (sp->max_instances > 0 && same >= sp->max_instances) slot = oldest;
    else if (free_slot >= 0) slot = free_slot;
    else if (quiet >= 0 && quiet_gain < l + r) slot = quiet;
    if (slot < 0) return 0;

    HcVoice* v = &m->voices[slot];
    memset(v, 0, sizeof(*v));
    v->samples = hc_sound_samples(p, sound, perm, &v->frames);
    float pitch = c->pitch_lo + (c->pitch_hi - c->pitch_lo) * rand01(m);
    if (sp->pitch > 0.0f) pitch *= sp->pitch;
    v->step = pitch * (float)HC_SOUND_RATE / (float)m->out_rate;
    v->params = *sp;
    v->params.gain *= c->perms[perm].gain;
    hc_mixer_spatial(m, &v->params, &v->target_l, &v->target_r);
    v->gain_l = v->target_l;
    v->gain_r = v->target_r;
    v->level = 1.0f;
    v->sound = sound;
    v->started = ++m->clock;
    /* Handles map back to their slot: handle % HC_MIX_VOICES == slot. */
    m->next_handle += HC_MIX_VOICES;
    if (m->next_handle > 0xF0000000u) m->next_handle = HC_MIX_VOICES;
    v->handle = m->next_handle + (unsigned)slot;
    return v->handle;
}

void hc_mixer_listener(HcMixer* m, float x, float y, float z, float yaw) {
    m->ear[0] = x;
    m->ear[1] = y;
    m->ear[2] = z;
    m->yaw = yaw;
    for (int i = 0; i < HC_MIX_VOICES; i++) {
        HcVoice* v = &m->voices[i];
        if (v->samples) hc_mixer_spatial(m, &v->params, &v->target_l, &v->target_r);
    }
}

static void mix_chunk(HcMixer* m, int16_t* out, int frames) {
    float acc[MIX_CHUNK * 2];
    memset(acc, 0, sizeof(float) * 2 * (size_t)frames);
    float inv = 1.0f / (float)frames;
    for (int i = 0; i < HC_MIX_VOICES; i++) {
        HcVoice* v = &m->voices[i];
        if (!v->samples) continue;
        float dl = (v->target_l - v->gain_l) * inv, dr = (v->target_r - v->gain_r) * inv;
        float gl = v->gain_l, gr = v->gain_r;
        double pos = v->pos;
        int k = 0;
        for (; k < frames; k++) {
            uint32_t i0 = (uint32_t)pos;
            if (i0 >= v->frames || v->level <= 0.0f) break;
            float frac = (float)(pos - (double)i0);
            float a = v->samples[i0];
            float b = i0 + 1 < v->frames ? v->samples[i0 + 1] : 0.0f;
            float s = (a + (b - a) * frac) * v->level;
            gl += dl;
            gr += dr;
            acc[2 * k] += s * gl;
            acc[2 * k + 1] += s * gr;
            pos += v->step;
            if (v->fade > 0.0f) v->level -= v->fade;
        }
        v->pos = pos;
        v->gain_l = v->target_l;
        v->gain_r = v->target_r;
        if (k < frames) v->samples = NULL;
    }
    float peak = 0.0f;
    for (int k = 0; k < frames * 2; k++) {
        float a = fabsf(acc[k]) * m->volume;
        if (a > peak) peak = a;
    }
    float want = peak * m->limiter > 32000.0f ? 32000.0f / peak : m->limiter + (1.0f - m->limiter) * 0.02f;
    if (want > 1.0f) want = 1.0f;
    float g0 = m->limiter, dg = (want - g0) * inv;
    if (want < g0) {
        g0 = want; /* clamp at once; release is gradual */
        dg = 0.0f;
    }
    for (int k = 0; k < frames; k++) {
        float g = (g0 + dg * (float)k) * m->volume;
        for (int ch = 0; ch < 2; ch++) {
            float s = acc[2 * k + ch] * g;
            out[2 * k + ch] = (int16_t)(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : (int)s);
        }
    }
    m->limiter = want;
}

void hc_mixer_mix(HcMixer* m, int16_t* out, int frames) {
    while (frames > 0) {
        int n = frames < MIX_CHUNK ? frames : MIX_CHUNK;
        mix_chunk(m, out, n);
        out += 2 * n;
        frames -= n;
    }
}
