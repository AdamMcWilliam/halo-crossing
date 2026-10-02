/* Sound pack and mixer checks. The mixer runs on a synthetic pack; the
 * imported pack (tools/import_halo.py) is checked when present. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/hc_sound.h"

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                               \
        }                                                               \
    } while (0)

#define TONE_FRAMES 22050

/* One second of 440 Hz in slot 0 and slot HC_SND_EXPL_FRAG, everything else empty. */
static void write_tone_pack(const char* path) {
    uint32_t slots_off = 16, perms_off = slots_off + 16 * HC_SND_COUNT, samples_off = perms_off + 32;
    size_t size = samples_off + TONE_FRAMES * 2;
    unsigned char* d = calloc(1, size);
    memcpy(d, "HCSD", 4);
    uint32_t head[3] = { 1, HC_SND_COUNT, slots_off };
    memcpy(d + 4, head, sizeof(head));
    int with_tone[2] = { 0, HC_SND_EXPL_FRAG };
    for (int k = 0; k < 2; k++) {
        unsigned char* s = d + slots_off + 16 * with_tone[k];
        uint16_t count = 1;
        uint32_t po = perms_off + 16 * k;
        float pitch[2] = { 1.0f, 1.0f };
        memcpy(s, &count, 2);
        memcpy(s + 4, &po, 4);
        memcpy(s + 8, pitch, 8);
        HcSoundPerm pm = { samples_off, TONE_FRAMES, 1.0f, 0 };
        memcpy(d + po, &pm, sizeof(pm));
    }
    int16_t* pcm = (int16_t*)(d + samples_off);
    for (int i = 0; i < TONE_FRAMES; i++) pcm[i] = (int16_t)(12000.0 * sin(i * 2.0 * 3.14159265 * 440.0 / 22050.0));
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, size, f);
        fclose(f);
    }
    free(d);
}

static void energy(const int16_t* buf, int frames, double* l, double* r) {
    *l = *r = 0.0;
    for (int i = 0; i < frames; i++) {
        *l += (double)buf[2 * i] * buf[2 * i];
        *r += (double)buf[2 * i + 1] * buf[2 * i + 1];
    }
}

static HcSoundParams at(float x, float y, float z) {
    HcSoundParams p = { 1, x, y, z, 1.0f, 1.0f, 30.0f, 0.0f, 0 };
    return p;
}

static int16_t g_buf[4096 * 2];

static void test_mixer(const char* dir) {
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/tone.hcpk", dir);
    write_tone_pack(path);
    HcSoundPack p;
    CHECK(hc_sound_pack_load(&p, path, err, sizeof(err)));
    if (!p.loaded) {
        fprintf(stderr, "tone pack: %s\n", err);
        return;
    }
    CHECK(hc_sound_has(&p, 0) && !hc_sound_has(&p, 1));

    HcMixer m;
    hc_mixer_init(&m, HC_SOUND_RATE, 7);
    hc_mixer_listener(&m, 0, 0, 0, 0.0f);
    double l, r;

    /* Yaw 0 faces +x; +y is left. */
    HcSoundParams sp = at(3, 2, 0);
    unsigned h = hc_mixer_play(&m, &p, 0, &sp);
    CHECK(h != 0 && hc_mixer_playing(&m, h));
    hc_mixer_mix(&m, g_buf, 2048);
    energy(g_buf, 2048, &l, &r);
    CHECK(l > r * 2.0);
    /* Turning to face the source centres it. */
    hc_mixer_listener(&m, 0, 0, 0, atan2f(2.0f, 3.0f));
    hc_mixer_mix(&m, g_buf, 2048);
    hc_mixer_mix(&m, g_buf, 2048);
    energy(g_buf, 2048, &l, &r);
    CHECK(fabs(l - r) < 0.05 * (l + r));
    hc_mixer_stop(&m, h, 0.0f);
    CHECK(!hc_mixer_playing(&m, h));

    hc_mixer_listener(&m, 0, 0, 0, 0.0f);
    sp = at(3, -2, 0);
    h = hc_mixer_play(&m, &p, 0, &sp);
    hc_mixer_mix(&m, g_buf, 2048);
    energy(g_buf, 2048, &l, &r);
    CHECK(r > l * 2.0);
    double near_energy = l + r;
    hc_mixer_stop(&m, h, 0.0f);

    /* Farther is quieter; past max_dist it doesn't play at all. */
    sp = at(20, -2, 0);
    h = hc_mixer_play(&m, &p, 0, &sp);
    hc_mixer_mix(&m, g_buf, 2048);
    energy(g_buf, 2048, &l, &r);
    CHECK(h != 0 && l + r < near_energy * 0.2);
    hc_mixer_stop(&m, h, 0.0f);
    sp = at(40, 0, 0);
    CHECK(hc_mixer_play(&m, &p, 0, &sp) == 0);
    CHECK(hc_mixer_play(&m, &p, 1, &sp) == 0); /* empty slot */

    /* Instance limit cuts the oldest. */
    sp = at(1, 0, 0);
    sp.max_instances = 3;
    unsigned first = hc_mixer_play(&m, &p, 0, &sp);
    for (int i = 0; i < 5; i++) hc_mixer_play(&m, &p, 0, &sp);
    CHECK(hc_mixer_active(&m) == 3);
    CHECK(!hc_mixer_playing(&m, first));

    /* Fades finish; voices end with their samples. */
    for (int i = 0; i < HC_MIX_VOICES; i++)
        if (m.voices[i].samples) hc_mixer_stop(&m, m.voices[i].handle, 0.05f);
    hc_mixer_mix(&m, g_buf, 4096);
    CHECK(hc_mixer_active(&m) == 0);
    sp = at(0.5f, 0, 0);
    h = hc_mixer_play(&m, &p, 0, &sp);
    for (int i = 0; i < 6; i++) hc_mixer_mix(&m, g_buf, 4096);
    CHECK(!hc_mixer_playing(&m, h));

    /* Pile-ups don't wrap around: the limiter holds the peak under full scale. */
    sp = at(0, 0, 0);
    for (int i = 0; i < 20; i++) hc_mixer_play(&m, &p, HC_SND_EXPL_FRAG, &sp);
    int peak = 0, flips = 0;
    for (int b = 0; b < 4; b++) {
        hc_mixer_mix(&m, g_buf, 2048);
        for (int i = 1; i < 2048; i++) {
            int a = abs(g_buf[2 * i]);
            if (a > peak) peak = a;
            /* A wrapped sample shows as a full-scale jump between neighbours. */
            if (abs(g_buf[2 * i] - g_buf[2 * i - 2]) > 40000) flips++;
        }
    }
    CHECK(peak > 20000 && peak <= 32767);
    CHECK(flips == 0);

    /* A full pool steals the quietest voice for a louder sound. */
    hc_mixer_init(&m, HC_SOUND_RATE, 3);
    sp = at(25, 0, 0);
    for (int i = 0; i < HC_MIX_VOICES; i++) CHECK(hc_mixer_play(&m, &p, 0, &sp) != 0);
    CHECK(hc_mixer_play(&m, &p, 0, &sp) == 0);
    sp = at(1, 0, 0);
    CHECK(hc_mixer_play(&m, &p, 0, &sp) != 0);
    hc_sound_pack_free(&p);

    write_tone_pack(path);
    FILE* f = fopen(path, "r+b");
    if (f) {
        fseek(f, 8, SEEK_SET);
        uint32_t wrong = HC_SND_COUNT + 1;
        fwrite(&wrong, 4, 1, f);
        fclose(f);
    }
    CHECK(!hc_sound_pack_load(&p, path, err, sizeof(err)));
    CHECK(strstr(err, "slot count") != NULL);
    CHECK(!hc_sound_pack_load(&p, "no/such/sounds.hcpk", err, sizeof(err)));
}

static void test_imported(const char* dir) {
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/" HC_SOUND_PACK_NAME, dir);
    HcSoundPack p;
    if (!hc_sound_pack_load(&p, path, err, sizeof(err))) {
        printf("sound pack: skipped (%s: %s)\n", path, err);
        return;
    }
    int present = 0, perms = 0;
    for (int i = 0; i < HC_SND_COUNT; i++) {
        present += hc_sound_has(&p, i);
        perms += p.clips[i].perm_count;
    }
    for (int w = 0; w < HC_SND_WEAPONS; w++) CHECK(hc_sound_has(&p, HC_SND_FIRE + w));
    CHECK(hc_sound_has(&p, HC_SND_EXPL_FRAG) && hc_sound_has(&p, HC_SND_EXPL_PLASMA_GRENADE));
    CHECK(hc_sound_has(&p, HC_SND_GRUNT + HC_LINE_ALERT) && hc_sound_has(&p, HC_SND_ELITE + HC_LINE_DEATH));
    CHECK(hc_sound_has(&p, HC_SND_GRUNT + HC_LINE_GRENADE_STUCK));
    /* Decoded audio, not silence or noise: the rifle shot starts loud and decays. */
    uint32_t frames = 0;
    const int16_t* s = hc_sound_samples(&p, HC_SND_FIRE, 0, &frames);
    CHECK(s && frames > 4000);
    if (s && frames > 4000) {
        double head = 0, tail = 0;
        for (uint32_t i = 0; i < 2000; i++) head += fabs((double)s[i]);
        for (uint32_t i = frames - 2000; i < frames; i++) tail += fabs((double)s[i]);
        CHECK(head > 2000.0 * 1000.0 && tail < head * 0.1);
    }
    printf("sound pack: %d sounds, %d permutations\n", present, perms);
    hc_sound_pack_free(&p);
}

int main(int argc, char** argv) {
    const char* tmp = argc > 1 ? argv[1] : ".";
    const char* dir = argc > 2 ? argv[2] : "assets_local/halo/generated";
    test_mixer(tmp);
    test_imported(dir);
    if (g_failures) {
        fprintf(stderr, "sound_test: %d failures\n", g_failures);
        return 1;
    }
    printf("sound_test: ok\n");
    return 0;
}
