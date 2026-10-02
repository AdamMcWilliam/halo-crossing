/* Skinned model packs written by tools/import_halo.py from the user's own
 * Halo disc (assets_local/, never in the repository):
 *   HCFP  first-person: the Chief's arms merged with each weapon, skinned to
 *         that weapon's animation skeleton. Space is Halo's first-person
 *         camera frame in world units: x forward, y left, z up, eye at the origin.
 *   HCBP  third-person Covenant and their weapons (hc_biped_view.h): Halo
 *         model space, feet at the origin.
 * Plain C so the headless tests build it. */
#ifndef HC_FP_PACK_H
#define HC_FP_PACK_H

#include <stddef.h>
#include <stdint.h>

#define HC_FP_WEAPON_SLOTS 10   /* HCFP: HaloWeaponId order */
#define HC_FP_MAX_SLOTS 16
#define HC_FP_MAX_ANIMS 40
#define HC_FP_MAX_NODES 64
#define HC_FP_MAX_LODS 4
#define HC_FP_MAX_BATCH_VERTICES 128
#define HC_FP_TEXTURE_CMPR 0
#define HC_FP_TEXTURE_RGBA8 1
#define HC_FP_BATCH_TRANSLUCENT 0x1
#define HC_FP_NO_TEXTURE 0xFFFF
#define HC_FP_FPS 30.0f         /* Halo animation tick rate */
#define HC_FP_ANIM_BASE 0
#define HC_FP_ANIM_OVERLAY 1

/* HCFP animation slots. Must match ANIM_SLOTS in tools/halo_import/fp_pack.py. */
typedef enum HcFpAnim {
    HC_FP_IDLE,
    HC_FP_FIRE,
    HC_FP_READY,
    HC_FP_RELOAD_FULL,
    HC_FP_RELOAD_EMPTY,     /* shotgun: one shell */
    HC_FP_THROW_GRENADE,
    HC_FP_OVERHEATED,
    HC_FP_POSING,
    HC_FP_RELOAD_ENTER,     /* shotgun */
    HC_FP_RELOAD_EXIT,      /* shotgun */
    HC_FP_FIRE_CHARGED,
    HC_FP_OVERHEATING,
    HC_FP_ANIM_COUNT
} HcFpAnim;

typedef struct HcFpTexture {
    uint16_t width, height, format, flags;
    uint32_t offset, size;
} HcFpTexture;

typedef struct HcFpVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    uint8_t node0, node1;   /* node1 0xFF: rigid */
    uint16_t weight0;       /* node0 share, /65535 */
} HcFpVertex;

typedef struct HcFpBatch {
    uint16_t texture;
    uint8_t vertex_count, flags;
    uint16_t triangle_count, pad;
    uint32_t rgba;
    float uv_offset[2];     /* subtracted so texture coordinates fit s10.5 */
    uint32_t indices_offset;    /* u16[vertex_count] into the model's vertices */
    uint32_t triangles_offset;  /* u8[triangle_count][3] into the batch */
} HcFpBatch;

/* One node's local transform; also the on-disk frame layout. */
typedef struct HcFpXf {
    float q[4];             /* i, j, k, w */
    float t[3];
    float s;
} HcFpXf;

typedef float HcFpMtx[3][4];

typedef struct HcFpModel {
    int node_count, batch_count, vertex_count, anim_count;
    const int16_t* parents;
    const HcFpMtx* inv_bind;
    const HcFpVertex* vertices;
    const HcFpBatch* batches;
    int anim_frames[HC_FP_MAX_ANIMS];
    int anim_kind[HC_FP_MAX_ANIMS];
    const HcFpXf* anim_data[HC_FP_MAX_ANIMS];     /* [frames][node_count] */
    const float (*anim_root[HC_FP_MAX_ANIMS])[4]; /* [frames] dx dy dz dyaw from frame 0, or NULL */
    float anim_distance[HC_FP_MAX_ANIMS];         /* horizontal root travel, wu */
    int muzzle_node;
    float muzzle[3];
    int hand_node;          /* -1: nothing can be held */
    HcFpXf hand;            /* held weapon's origin, in hand_node space */
    int lod_count;
    int lod_batch_end[HC_FP_MAX_LODS], lod_vertex_end[HC_FP_MAX_LODS];
    const uint8_t* base;
} HcFpModel;

typedef struct HcFpPack {
    uint8_t* data;
    size_t size;
    void* allocation;
    int texture_count;
    const HcFpTexture* textures;
    int slot_count;
    int has[HC_FP_MAX_SLOTS];
    HcFpModel models[HC_FP_MAX_SLOTS];
} HcFpPack;

/* 1 on success. The file must carry `magic` with exactly `slots` model slots
 * and `anims` animation slots. On failure the pack is left empty and `err`
 * says why. */
int hc_fp_pack_load(HcFpPack* pack, const char* path, const char magic[4], int slots, int anims, char* err,
                    size_t err_size);
void hc_fp_pack_free(HcFpPack* pack);

const HcFpModel* hc_fp_pack_model(const HcFpPack* pack, int slot);
const void* hc_fp_texture_pixels(const HcFpPack* pack, int texture);

static inline const uint16_t* hc_fp_batch_indices(const HcFpModel* w, const HcFpBatch* b) {
    return (const uint16_t*)(w->base + b->indices_offset);
}
static inline const uint8_t* hc_fp_batch_triangles(const HcFpModel* w, const HcFpBatch* b) {
    return w->base + b->triangles_offset;
}
static inline int hc_fp_has_anim(const HcFpModel* w, int anim) {
    return anim >= 0 && anim < w->anim_count && w->anim_frames[anim] > 0;
}

/* Local transforms at a fractional frame. Looping animations wrap the last
 * frame into the first; one-shots hold the last frame. Missing slots give
 * the idle pose (slot 0, frame 0). */
void hc_fp_sample(const HcFpModel* w, int anim, float frame, int loop, HcFpXf* out);
/* Adds an overlay animation's change since its first frame onto `local`,
 * scaled by `weight` 0..1. */
void hc_fp_overlay(const HcFpModel* w, int anim, float frame, float weight, HcFpXf* local);
/* Root motion accumulated by `frame` (one-shot, clamped): dx dy dz dyaw. */
void hc_fp_root(const HcFpModel* w, int anim, float frame, float out[4]);
/* a = a..b by t (nlerp rotations). */
void hc_fp_blend(HcFpXf* a, const HcFpXf* b, float t, int count);
/* World transform per node and the skinning matrix (world * inverse bind). */
void hc_fp_pose(const HcFpModel* w, const HcFpXf* local, HcFpMtx* world, HcFpMtx* skin);
void hc_fp_skin_vertex(const HcFpMtx* skin, const HcFpVertex* v, float pos[3], float normal[3]);
void hc_fp_mtx_point(const HcFpMtx m, const float p[3], float out[3]);
void hc_fp_xf_to_mtx(const HcFpXf* x, HcFpMtx out);
void hc_fp_mtx_mul(const HcFpMtx a, const HcFpMtx b, HcFpMtx out);

#endif
