/* First-person pack loader checks. The structural checks always run; the
 * pose checks need a pack built by tools/import_halo.py from the user's own
 * disc and are skipped without one. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/hc_fp_pack.h"

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                               \
        }                                                               \
    } while (0)

static void write_file(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, size, f);
        fclose(f);
    }
}

static void test_rejects_bad_files(const char* dir) {
    HcFpPack p;
    char err[128], path[512];
    CHECK(!hc_fp_pack_load(&p, "no/such/pack.hcpk", err, sizeof(err)));
    CHECK(strstr(err, "not found") != NULL);

    snprintf(path, sizeof(path), "%s/bad_magic.hcpk", dir);
    unsigned char junk[64];
    memset(junk, 0, sizeof(junk));
    memcpy(junk, "NOPE", 4);
    write_file(path, junk, sizeof(junk));
    CHECK(!hc_fp_pack_load(&p, path, err, sizeof(err)));
    CHECK(strstr(err, "magic") != NULL);
    CHECK(p.data == NULL);

    snprintf(path, sizeof(path), "%s/truncated.hcpk", dir);
    write_file(path, "HCFP", 4);
    CHECK(!hc_fp_pack_load(&p, path, err, sizeof(err)));
}

static int finite3(const float v[3]) {
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static void test_real_pack(const char* path) {
    HcFpPack p;
    char err[128];
    if (!hc_fp_pack_load(&p, path, err, sizeof(err))) {
        printf("fp pack: skipped (%s: %s)\n", path, err);
        return;
    }
    static HcFpXf local[HC_FP_MAX_NODES];
    static HcFpMtx world[HC_FP_MAX_NODES], skin[HC_FP_MAX_NODES];
    int weapons = 0;
    for (int id = 0; id < HC_FP_WEAPON_SLOTS; id++) {
        const HcFpWeapon* w = hc_fp_pack_weapon(&p, id);
        if (!w) continue;
        weapons++;
        CHECK(w->batch_count > 0 && w->vertex_count > 0);
        CHECK(w->anim_frames[HC_FP_IDLE] > 0 && w->anim_frames[HC_FP_READY] > 0);
        for (int a = 0; a < HC_FP_ANIM_COUNT; a++) {
            if (w->anim_frames[a] == 0) continue;
            float frames = (float)w->anim_frames[a];
            float probes[3] = { 0.0f, frames * 0.5f + 0.25f, frames - 1.0f };
            for (int k = 0; k < 3; k++) {
                hc_fp_sample(w, (HcFpAnim)a, probes[k], a == HC_FP_IDLE, local);
                hc_fp_pose(w, local, world, skin);
                int ahead = 0, bad = 0;
                for (int v = 0; v < w->vertex_count; v++) {
                    float pos[3], nrm[3];
                    hc_fp_skin_vertex(skin, &w->vertices[v], pos, nrm);
                    if (!finite3(pos) || !finite3(nrm) || fabsf(pos[0]) > 4.0f) bad++;
                    if (pos[0] > 0.0f) ahead++;
                }
                CHECK(bad == 0);
                /* Idle holds the weapon out in front of the eye. */
                if (a == HC_FP_IDLE) CHECK(ahead * 2 > w->vertex_count);
            }
        }
        hc_fp_sample(w, HC_FP_IDLE, 0.0f, 1, local);
        hc_fp_pose(w, local, world, NULL);
        if (w->muzzle_node >= 0) {
            float m[3];
            hc_fp_mtx_point(world[w->muzzle_node], w->muzzle, m);
            CHECK(m[0] > 0.05f && m[0] < 1.0f);
        }
        /* Looping wraps; one-shots clamp. */
        HcFpXf a[HC_FP_MAX_NODES], b[HC_FP_MAX_NODES];
        int n = w->anim_frames[HC_FP_IDLE];
        hc_fp_sample(w, HC_FP_IDLE, (float)n, 1, a);
        hc_fp_sample(w, HC_FP_IDLE, 0.0f, 1, b);
        CHECK(fabsf(a[0].t[0] - b[0].t[0]) < 1e-5f && fabsf(a[0].q[3] - b[0].q[3]) < 1e-5f);
        hc_fp_sample(w, HC_FP_READY, 1e6f, 0, a);
        hc_fp_sample(w, HC_FP_READY, (float)(w->anim_frames[HC_FP_READY] - 1), 0, b);
        CHECK(memcmp(a, b, sizeof(HcFpXf) * (size_t)w->node_count) == 0);
    }
    CHECK(weapons >= 9);
    CHECK(hc_fp_pack_weapon(&p, 9) == NULL); /* Xbox CE has no first-person fuel rod */
    for (int t = 0; t < p.texture_count; t++) CHECK(((uintptr_t)hc_fp_texture_pixels(&p, t) & 31) == 0);
    printf("fp pack: %d weapons, %d textures checked\n", weapons, p.texture_count);
    hc_fp_pack_free(&p);
}

int main(int argc, char** argv) {
    const char* tmp = argc > 1 ? argv[1] : ".";
    const char* pack = argc > 2 ? argv[2] : "assets_local/halo/generated/fp_weapons.hcpk";
    test_rejects_bad_files(tmp);
    test_real_pack(pack);
    if (g_failures) {
        fprintf(stderr, "fp_pack_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("fp_pack_test: ok\n");
    return 0;
}
