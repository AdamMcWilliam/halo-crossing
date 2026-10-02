#include "hc_fp_pack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_VERSION 1
#define HEADER_SIZE 32
#define WEAPON_HEADER_SIZE 44

typedef struct PackHeader {
    char magic[4];
    uint32_t version, weapon_slots, texture_count, textures_offset, weapons_offset, anim_slots, reserved;
} PackHeader;

typedef struct WeaponHeader {
    uint16_t node_count, batch_count;
    uint32_t vertex_count, parents_offset, inv_bind_offset, vertices_offset, batches_offset, anims_offset;
    int16_t muzzle_node;
    uint16_t pad;
    float muzzle[3];
} WeaponHeader;

typedef struct AnimSlot {
    uint16_t frames, flags;
    uint32_t offset;
} AnimSlot;

typedef char check_header[sizeof(PackHeader) == HEADER_SIZE ? 1 : -1];
typedef char check_weapon[sizeof(WeaponHeader) == WEAPON_HEADER_SIZE ? 1 : -1];
typedef char check_vertex[sizeof(HcFpVertex) == 36 ? 1 : -1];
typedef char check_batch[sizeof(HcFpBatch) == 28 ? 1 : -1];
typedef char check_xf[sizeof(HcFpXf) == 32 ? 1 : -1];

static int fail(char* err, size_t n, const char* msg, int weapon) {
    if (err && n) {
        if (weapon >= 0) snprintf(err, n, "weapon %d: %s", weapon, msg);
        else snprintf(err, n, "%s", msg);
    }
    return 0;
}

static int in_range(const HcFpPack* p, uint32_t off, size_t bytes) {
    return off <= p->size && bytes <= p->size - off;
}

static int load_weapon(HcFpPack* p, int slot, uint32_t off, uint32_t anim_slots, char* err, size_t n) {
    if ((off & 3) || !in_range(p, off, sizeof(WeaponHeader))) return fail(err, n, "header out of range", slot);
    const WeaponHeader* h = (const WeaponHeader*)(p->data + off);
    HcFpWeapon* w = &p->weapons[slot];
    memset(w, 0, sizeof(*w));
    if (h->node_count == 0 || h->node_count > HC_FP_MAX_NODES) return fail(err, n, "node count", slot);
    if (!in_range(p, h->parents_offset, h->node_count * sizeof(int16_t)) ||
        !in_range(p, h->inv_bind_offset, h->node_count * sizeof(HcFpMtx)) ||
        !in_range(p, h->vertices_offset, (size_t)h->vertex_count * sizeof(HcFpVertex)) ||
        !in_range(p, h->batches_offset, (size_t)h->batch_count * sizeof(HcFpBatch)) ||
        !in_range(p, h->anims_offset, anim_slots * sizeof(AnimSlot)) ||
        ((h->parents_offset | h->inv_bind_offset | h->vertices_offset | h->batches_offset | h->anims_offset) & 3))
        return fail(err, n, "table out of range", slot);

    w->node_count = h->node_count;
    w->batch_count = h->batch_count;
    w->vertex_count = (int)h->vertex_count;
    w->parents = (const int16_t*)(p->data + h->parents_offset);
    w->inv_bind = (const HcFpMtx*)(p->data + h->inv_bind_offset);
    w->vertices = (const HcFpVertex*)(p->data + h->vertices_offset);
    w->batches = (const HcFpBatch*)(p->data + h->batches_offset);
    w->base = p->data;
    w->muzzle_node = h->muzzle_node < h->node_count ? h->muzzle_node : -1;
    memcpy(w->muzzle, h->muzzle, sizeof(w->muzzle));

    for (int i = 0; i < w->node_count; i++) {
        if (w->parents[i] >= i) return fail(err, n, "parent after child", slot);
    }
    for (int i = 0; i < w->vertex_count; i++) {
        const HcFpVertex* v = &w->vertices[i];
        if (v->node0 >= w->node_count || (v->node1 != 0xFF && v->node1 >= w->node_count))
            return fail(err, n, "vertex node", slot);
    }
    for (int i = 0; i < w->batch_count; i++) {
        const HcFpBatch* b = &w->batches[i];
        if (b->vertex_count == 0 || b->vertex_count > HC_FP_MAX_BATCH_VERTICES) return fail(err, n, "batch size", slot);
        if (b->texture != HC_FP_NO_TEXTURE && b->texture >= p->texture_count) return fail(err, n, "batch texture", slot);
        if ((b->indices_offset & 1) || !in_range(p, b->indices_offset, b->vertex_count * sizeof(uint16_t)) ||
            !in_range(p, b->triangles_offset, (size_t)b->triangle_count * 3))
            return fail(err, n, "batch out of range", slot);
        const uint16_t* idx = hc_fp_batch_indices(w, b);
        for (int k = 0; k < b->vertex_count; k++)
            if (idx[k] >= w->vertex_count) return fail(err, n, "batch index", slot);
        const uint8_t* tri = hc_fp_batch_triangles(w, b);
        for (int k = 0; k < b->triangle_count * 3; k++)
            if (tri[k] >= b->vertex_count) return fail(err, n, "batch triangle", slot);
    }
    const AnimSlot* slots = (const AnimSlot*)(p->data + h->anims_offset);
    for (uint32_t a = 0; a < anim_slots && a < HC_FP_ANIM_COUNT; a++) {
        if (slots[a].frames == 0) continue;
        if ((slots[a].offset & 3) ||
            !in_range(p, slots[a].offset, (size_t)slots[a].frames * w->node_count * sizeof(HcFpXf)))
            return fail(err, n, "animation out of range", slot);
        w->anim_frames[a] = slots[a].frames;
        w->anim_data[a] = (const HcFpXf*)(p->data + slots[a].offset);
    }
    if (w->anim_frames[HC_FP_IDLE] == 0) return fail(err, n, "no idle animation", slot);
    return 1;
}

int hc_fp_pack_load(HcFpPack* p, const char* path, char* err, size_t n) {
    memset(p, 0, sizeof(*p));
    FILE* f = fopen(path, "rb");
    if (f == NULL) return fail(err, n, "not found", -1);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < HEADER_SIZE) {
        fclose(f);
        return fail(err, n, "truncated", -1);
    }
    /* Textures are 32-byte aligned within the file; keep that in memory. */
    p->allocation = malloc((size_t)size + 31);
    if (p->allocation == NULL) {
        fclose(f);
        return fail(err, n, "out of memory", -1);
    }
    p->data = (uint8_t*)(((uintptr_t)p->allocation + 31) & ~(uintptr_t)31);
    p->size = (size_t)size;
    size_t got = fread(p->data, 1, p->size, f);
    fclose(f);
    if (got != p->size) {
        hc_fp_pack_free(p);
        return fail(err, n, "read error", -1);
    }

    const PackHeader* h = (const PackHeader*)p->data;
    int ok = 1;
    if (memcmp(h->magic, "HCFP", 4) != 0) ok = fail(err, n, "bad magic", -1);
    else if (h->version != PACK_VERSION) ok = fail(err, n, "unsupported version (re-run tools/import_halo.py)", -1);
    else if (h->weapon_slots != HC_FP_WEAPON_SLOTS || h->anim_slots != HC_FP_ANIM_COUNT)
        ok = fail(err, n, "slot layout mismatch (re-run tools/import_halo.py)", -1);
    else if (!in_range(p, h->textures_offset, (size_t)h->texture_count * sizeof(HcFpTexture)) ||
             !in_range(p, h->weapons_offset, HC_FP_WEAPON_SLOTS * sizeof(uint32_t)))
        ok = fail(err, n, "tables out of range", -1);
    if (ok) {
        p->texture_count = (int)h->texture_count;
        p->textures = (const HcFpTexture*)(p->data + h->textures_offset);
        for (int i = 0; ok && i < p->texture_count; i++) {
            const HcFpTexture* t = &p->textures[i];
            int cmpr = t->format == HC_FP_TEXTURE_CMPR;
            size_t texels = (size_t)t->width * t->height;
            size_t bytes = cmpr ? texels / 2 : texels * 4;
            int tile = cmpr ? 7 : 3;
            if (t->format > HC_FP_TEXTURE_RGBA8 || t->width == 0 || t->height == 0 || t->width > 1024 ||
                t->height > 1024 || (t->width & tile) || (t->height & tile) || (t->offset & 31) ||
                t->size < bytes || !in_range(p, t->offset, t->size))
                ok = fail(err, n, "bad texture", -1);
        }
        const uint32_t* offs = (const uint32_t*)(p->data + h->weapons_offset);
        for (int i = 0; ok && i < HC_FP_WEAPON_SLOTS; i++) {
            if (offs[i] == 0) continue;
            ok = load_weapon(p, i, offs[i], h->anim_slots, err, n);
            p->has[i] = ok;
        }
    }
    if (!ok) {
        char keep[128] = "";
        if (err && n) snprintf(keep, sizeof(keep), "%s", err);
        hc_fp_pack_free(p);
        if (err && n) snprintf(err, n, "%s", keep);
    }
    return ok;
}

void hc_fp_pack_free(HcFpPack* p) {
    free(p->allocation);
    memset(p, 0, sizeof(*p));
}

const HcFpWeapon* hc_fp_pack_weapon(const HcFpPack* p, int id) {
    if (p == NULL || id < 0 || id >= HC_FP_WEAPON_SLOTS || !p->has[id]) return NULL;
    return &p->weapons[id];
}

const void* hc_fp_texture_pixels(const HcFpPack* p, int t) {
    if (t < 0 || t >= p->texture_count) return NULL;
    return p->data + p->textures[t].offset;
}

/* ---- pose ------------------------------------------------------------------ */

static void quat_nlerp(float out[4], const float a[4], const float b[4], float t) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float s = d < 0.0f ? -t : t;
    float q[4], len = 0.0f;
    for (int i = 0; i < 4; i++) {
        q[i] = a[i] * (1.0f - t) + b[i] * s;
        len += q[i] * q[i];
    }
    len = len > 1e-12f ? 1.0f / sqrtf(len) : 0.0f;
    for (int i = 0; i < 4; i++) out[i] = q[i] * len;
}

static void xf_lerp(HcFpXf* out, const HcFpXf* a, const HcFpXf* b, float t) {
    quat_nlerp(out->q, a->q, b->q, t);
    for (int i = 0; i < 3; i++) out->t[i] = a->t[i] + (b->t[i] - a->t[i]) * t;
    out->s = a->s + (b->s - a->s) * t;
}

void hc_fp_sample(const HcFpWeapon* w, HcFpAnim anim, float frame, int loop, HcFpXf* out) {
    if (anim < 0 || anim >= HC_FP_ANIM_COUNT || w->anim_frames[anim] == 0) {
        anim = HC_FP_IDLE;
        frame = 0.0f;
    }
    int frames = w->anim_frames[anim];
    const HcFpXf* data = w->anim_data[anim];
    int n = w->node_count;
    int f0, f1;
    float t;
    if (frames == 1) {
        f0 = f1 = 0;
        t = 0.0f;
    } else if (loop) {
        float fl = fmodf(frame, (float)frames);
        if (fl < 0.0f) fl += (float)frames;
        f0 = (int)fl;
        if (f0 >= frames) f0 = frames - 1;
        f1 = (f0 + 1) % frames;
        t = fl - (float)f0;
    } else {
        float last = (float)(frames - 1);
        float fl = frame < 0.0f ? 0.0f : (frame > last ? last : frame);
        f0 = (int)fl;
        if (f0 > frames - 2) f0 = frames - 2;
        f1 = f0 + 1;
        t = fl - (float)f0;
    }
    const HcFpXf* a = data + (size_t)f0 * n;
    const HcFpXf* b = data + (size_t)f1 * n;
    for (int i = 0; i < n; i++) xf_lerp(&out[i], &a[i], &b[i], t);
}

void hc_fp_blend(HcFpXf* a, const HcFpXf* b, float t, int count) {
    if (t <= 0.0f) return;
    if (t >= 1.0f) {
        memcpy(a, b, sizeof(*a) * (size_t)count);
        return;
    }
    for (int i = 0; i < count; i++) xf_lerp(&a[i], &a[i], &b[i], t);
}

/* Halo quaternions rotate the other way from the textbook formula. */
static void xf_to_mtx(const HcFpXf* x, HcFpMtx m) {
    float i = x->q[0], j = x->q[1], k = x->q[2], w = x->q[3], s = x->s;
    m[0][0] = (1.0f - 2.0f * (j * j + k * k)) * s;
    m[0][1] = 2.0f * (i * j + w * k) * s;
    m[0][2] = 2.0f * (i * k - w * j) * s;
    m[1][0] = 2.0f * (i * j - w * k) * s;
    m[1][1] = (1.0f - 2.0f * (i * i + k * k)) * s;
    m[1][2] = 2.0f * (j * k + w * i) * s;
    m[2][0] = 2.0f * (i * k + w * j) * s;
    m[2][1] = 2.0f * (j * k - w * i) * s;
    m[2][2] = (1.0f - 2.0f * (i * i + j * j)) * s;
    m[0][3] = x->t[0];
    m[1][3] = x->t[1];
    m[2][3] = x->t[2];
}

static void mtx_mul(const HcFpMtx a, const HcFpMtx b, HcFpMtx out) {
    HcFpMtx r;
    for (int row = 0; row < 3; row++) {
        for (int c = 0; c < 4; c++) {
            r[row][c] = a[row][0] * b[0][c] + a[row][1] * b[1][c] + a[row][2] * b[2][c] + (c == 3 ? a[row][3] : 0.0f);
        }
    }
    memcpy(out, r, sizeof(r));
}

void hc_fp_pose(const HcFpWeapon* w, const HcFpXf* local, HcFpMtx* world, HcFpMtx* skin) {
    for (int i = 0; i < w->node_count; i++) {
        HcFpMtx m;
        xf_to_mtx(&local[i], m);
        int parent = w->parents[i];
        if (parent >= 0) mtx_mul(world[parent], m, world[i]);
        else memcpy(world[i], m, sizeof(m));
        if (skin) mtx_mul(world[i], w->inv_bind[i], skin[i]);
    }
}

void hc_fp_mtx_point(const HcFpMtx m, const float p[3], float out[3]) {
    for (int r = 0; r < 3; r++) out[r] = m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3];
}

void hc_fp_skin_vertex(const HcFpMtx* skin, const HcFpVertex* v, float pos[3], float normal[3]) {
    const float* a = &skin[v->node0][0][0];
    float p[3], nrm[3];
    if (v->node1 == 0xFF || v->weight0 == 65535) {
        for (int r = 0; r < 3; r++) {
            const float* row = a + r * 4;
            p[r] = row[0] * v->pos[0] + row[1] * v->pos[1] + row[2] * v->pos[2] + row[3];
            nrm[r] = row[0] * v->normal[0] + row[1] * v->normal[1] + row[2] * v->normal[2];
        }
    } else {
        const float* b = &skin[v->node1][0][0];
        float wa = (float)v->weight0 * (1.0f / 65535.0f), wb = 1.0f - wa;
        for (int r = 0; r < 3; r++) {
            const float* ra = a + r * 4;
            const float* rb = b + r * 4;
            float m0 = ra[0] * wa + rb[0] * wb, m1 = ra[1] * wa + rb[1] * wb, m2 = ra[2] * wa + rb[2] * wb;
            p[r] = m0 * v->pos[0] + m1 * v->pos[1] + m2 * v->pos[2] + ra[3] * wa + rb[3] * wb;
            nrm[r] = m0 * v->normal[0] + m1 * v->normal[1] + m2 * v->normal[2];
        }
    }
    memcpy(pos, p, sizeof(p));
    if (normal) {
        float l = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        l = l > 1e-8f ? 1.0f / l : 0.0f;
        for (int r = 0; r < 3; r++) normal[r] = nrm[r] * l;
    }
}
