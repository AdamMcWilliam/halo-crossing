/* Skinned pack loader checks. The structural checks always run; the pose
 * checks need packs built by tools/import_halo.py from the user's own disc
 * and are skipped without them. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/hc_biped_pack.h"
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

static int load_fp(HcFpPack* p, const char* path, char* err, size_t n) {
    return hc_fp_pack_load(p, path, "HCFP", HC_FP_WEAPON_SLOTS, HC_FP_ANIM_COUNT, err, n);
}

static void test_rejects_bad_files(const char* dir) {
    HcFpPack p;
    char err[128], path[512];
    CHECK(!load_fp(&p, "no/such/pack.hcpk", err, sizeof(err)));
    CHECK(strstr(err, "not found") != NULL);

    snprintf(path, sizeof(path), "%s/bad_magic.hcpk", dir);
    unsigned char junk[64];
    memset(junk, 0, sizeof(junk));
    memcpy(junk, "NOPE", 4);
    write_file(path, junk, sizeof(junk));
    CHECK(!load_fp(&p, path, err, sizeof(err)));
    CHECK(strstr(err, "magic") != NULL);
    CHECK(p.data == NULL);

    snprintf(path, sizeof(path), "%s/truncated.hcpk", dir);
    write_file(path, "HCFP", 4);
    CHECK(!load_fp(&p, path, err, sizeof(err)));
}

static int finite3(const float v[3]) {
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static HcFpXf g_local[HC_FP_MAX_NODES];
static HcFpMtx g_world[HC_FP_MAX_NODES], g_skin[HC_FP_MAX_NODES];

/* Skins every vertex at a few frames of every animation; each must stay
 * finite and within `max_extent` of the origin. */
static void check_all_poses(const HcFpModel* w, float max_extent) {
    for (int a = 0; a < w->anim_count; a++) {
        if (w->anim_frames[a] == 0) continue;
        float frames = (float)w->anim_frames[a];
        float probes[3] = { 0.0f, frames * 0.5f + 0.25f, frames - 1.0f };
        for (int k = 0; k < 3; k++) {
            hc_fp_sample(w, 0, probes[k], 1, g_local);
            if (w->anim_kind[a] == HC_FP_ANIM_OVERLAY) hc_fp_overlay(w, a, probes[k], 1.0f, g_local);
            else hc_fp_sample(w, a, probes[k], a == 0, g_local);
            hc_fp_pose(w, g_local, g_world, g_skin);
            int bad = 0;
            for (int v = 0; v < w->vertex_count; v++) {
                float pos[3], nrm[3];
                hc_fp_skin_vertex(g_skin, &w->vertices[v], pos, nrm);
                if (!finite3(pos) || !finite3(nrm) || fabsf(pos[0]) > max_extent || fabsf(pos[1]) > max_extent ||
                    fabsf(pos[2]) > max_extent)
                    bad++;
            }
            CHECK(bad == 0);
        }
    }
}

static void test_fp_pack(const char* path) {
    HcFpPack p;
    char err[128];
    if (!load_fp(&p, path, err, sizeof(err))) {
        printf("fp pack: skipped (%s: %s)\n", path, err);
        return;
    }
    int weapons = 0;
    for (int id = 0; id < HC_FP_WEAPON_SLOTS; id++) {
        const HcFpModel* w = hc_fp_pack_model(&p, id);
        if (!w) continue;
        weapons++;
        CHECK(w->batch_count > 0 && w->vertex_count > 0 && w->lod_count == 1);
        CHECK(w->anim_frames[HC_FP_IDLE] > 0 && w->anim_frames[HC_FP_READY] > 0);
        check_all_poses(w, 4.0f);
        /* Idle holds the weapon out in front of the eye. */
        hc_fp_sample(w, HC_FP_IDLE, 0.0f, 1, g_local);
        hc_fp_pose(w, g_local, g_world, g_skin);
        int ahead = 0;
        for (int v = 0; v < w->vertex_count; v++) {
            float pos[3];
            hc_fp_skin_vertex(g_skin, &w->vertices[v], pos, NULL);
            ahead += pos[0] > 0.0f;
        }
        CHECK(ahead * 2 > w->vertex_count);
        if (w->muzzle_node >= 0) {
            float m[3];
            hc_fp_mtx_point(g_world[w->muzzle_node], w->muzzle, m);
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
    CHECK(hc_fp_pack_model(&p, 9) == NULL); /* Xbox CE has no first-person fuel rod */
    for (int t = 0; t < p.texture_count; t++) CHECK(((uintptr_t)hc_fp_texture_pixels(&p, t) & 31) == 0);
    printf("fp pack: %d weapons, %d textures checked\n", weapons, p.texture_count);
    hc_fp_pack_free(&p);
}

static void test_biped_pack(const char* path) {
    HcFpPack p;
    char err[128];
    if (!hc_fp_pack_load(&p, path, "HCBP", HC_BP_MODEL_COUNT, HC_BP_ANIM_COUNT, err, sizeof(err))) {
        printf("biped pack: skipped (%s: %s)\n", path, err);
        return;
    }
    static const float height[2] = { 0.58f, 0.79f }; /* Grunt, Elite */
    for (int c = HC_BP_GRUNT; c <= HC_BP_ELITE; c++) {
        const HcFpModel* m = hc_fp_pack_model(&p, c);
        CHECK(m != NULL);
        if (!m) continue;
        CHECK(m->lod_count == 3 && m->hand_node >= 0);
        CHECK(m->lod_vertex_end[0] > m->lod_vertex_end[1] - m->lod_vertex_end[0]);
        static const int needed[] = { HC_BP_IDLE, HC_BP_MOVE_FRONT, HC_BP_MOVE_BACK, HC_BP_MOVE_LEFT,
                                      HC_BP_MOVE_RIGHT, HC_BP_DIE_FRONT, HC_BP_DIE_BACK, HC_BP_FIRE_PISTOL };
        for (size_t k = 0; k < sizeof(needed) / sizeof(needed[0]); k++) CHECK(hc_fp_has_anim(m, needed[k]));
        CHECK(m->anim_kind[HC_BP_FIRE_PISTOL] == HC_FP_ANIM_OVERLAY && m->anim_kind[HC_BP_IDLE] == HC_FP_ANIM_BASE);
        CHECK(m->anim_distance[HC_BP_MOVE_FRONT] > 0.3f);
        check_all_poses(m, 2.5f);
        /* Standing on the origin, about as tall as the Halo model. */
        hc_fp_sample(m, HC_BP_IDLE, 0.0f, 1, g_local);
        hc_fp_pose(m, g_local, g_world, g_skin);
        float lo = 1e9f, hi = -1e9f;
        for (int v = 0; v < m->lod_vertex_end[0]; v++) {
            float pos[3];
            hc_fp_skin_vertex(g_skin, &m->vertices[v], pos, NULL);
            lo = fminf(lo, pos[2]);
            hi = fmaxf(hi, pos[2]);
        }
        CHECK(lo > -0.05f && lo < 0.05f);
        CHECK(fabsf(hi - height[c]) < 0.15f);
        /* A one-shot death travels; its root offset stops at the last frame. */
        float r0[4], r1[4];
        hc_fp_root(m, HC_BP_DIE_FRONT, 0.0f, r0);
        hc_fp_root(m, HC_BP_DIE_FRONT, 1e6f, r1);
        CHECK(r0[0] == 0.0f && r0[1] == 0.0f);
        CHECK(fabsf(r1[0]) + fabsf(r1[1]) > 0.05f);
    }
    for (int wi = HC_BP_PLASMA_PISTOL; wi < HC_BP_MODEL_COUNT; wi++) {
        const HcFpModel* m = hc_fp_pack_model(&p, wi);
        CHECK(m != NULL);
        if (!m) continue;
        CHECK(m->node_count == 1 && m->lod_count == 3 && m->muzzle[0] > 0.05f);
        check_all_poses(m, 1.0f);
    }
    printf("biped pack: %d textures checked\n", p.texture_count);
    hc_fp_pack_free(&p);
}

int main(int argc, char** argv) {
    const char* tmp = argc > 1 ? argv[1] : ".";
    const char* dir = argc > 2 ? argv[2] : "assets_local/halo/generated";
    char path[512];
    test_rejects_bad_files(tmp);
    snprintf(path, sizeof(path), "%s/fp_weapons.hcpk", dir);
    test_fp_pack(path);
    snprintf(path, sizeof(path), "%s/%s", dir, HC_BP_PACK_NAME);
    test_biped_pack(path);
    if (g_failures) {
        fprintf(stderr, "fp_pack_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("fp_pack_test: ok\n");
    return 0;
}
