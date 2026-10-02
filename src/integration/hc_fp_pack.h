/* First-person weapon pack written by tools/import_halo.py from the user's
 * own Halo disc (assets_local/, never in the repository): the Chief's arms
 * merged with each weapon, skinned to that weapon's animation skeleton.
 *
 * Space is Halo's first-person camera frame in world units: x forward,
 * y left, z up, eye at the origin. Plain C so the headless tests build it. */
#ifndef HC_FP_PACK_H
#define HC_FP_PACK_H

#include <stddef.h>
#include <stdint.h>

#define HC_FP_WEAPON_SLOTS 10   /* HaloWeaponId order */
#define HC_FP_MAX_NODES 64
#define HC_FP_MAX_BATCH_VERTICES 128
#define HC_FP_TEXTURE_CMPR 0
#define HC_FP_TEXTURE_RGBA8 1
#define HC_FP_BATCH_TRANSLUCENT 0x1
#define HC_FP_NO_TEXTURE 0xFFFF
#define HC_FP_FPS 30.0f         /* Halo animation tick rate */

/* Must match ANIM_SLOTS in tools/halo_import/fp_pack.py. */
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
    uint32_t indices_offset;    /* u16[vertex_count] into the weapon's vertices */
    uint32_t triangles_offset;  /* u8[triangle_count][3] into the batch */
} HcFpBatch;

/* One node's local transform; also the on-disk frame layout. */
typedef struct HcFpXf {
    float q[4];             /* i, j, k, w */
    float t[3];
    float s;
} HcFpXf;

typedef float HcFpMtx[3][4];

typedef struct HcFpWeapon {
    int node_count, batch_count, vertex_count;
    const int16_t* parents;
    const HcFpMtx* inv_bind;
    const HcFpVertex* vertices;
    const HcFpBatch* batches;
    int anim_frames[HC_FP_ANIM_COUNT];
    const HcFpXf* anim_data[HC_FP_ANIM_COUNT];  /* [frames][node_count] */
    int muzzle_node;
    float muzzle[3];
    const uint8_t* base;
} HcFpWeapon;

typedef struct HcFpPack {
    uint8_t* data;
    size_t size;
    void* allocation;
    int texture_count;
    const HcFpTexture* textures;
    int has[HC_FP_WEAPON_SLOTS];
    HcFpWeapon weapons[HC_FP_WEAPON_SLOTS];
} HcFpPack;

/* 1 on success. On failure the pack is left empty and `err` says why. */
int hc_fp_pack_load(HcFpPack* pack, const char* path, char* err, size_t err_size);
void hc_fp_pack_free(HcFpPack* pack);

const HcFpWeapon* hc_fp_pack_weapon(const HcFpPack* pack, int weapon_id);
const void* hc_fp_texture_pixels(const HcFpPack* pack, int texture);

static inline const uint16_t* hc_fp_batch_indices(const HcFpWeapon* w, const HcFpBatch* b) {
    return (const uint16_t*)(w->base + b->indices_offset);
}
static inline const uint8_t* hc_fp_batch_triangles(const HcFpWeapon* w, const HcFpBatch* b) {
    return w->base + b->triangles_offset;
}

/* Local transforms at a fractional frame. Looping animations wrap the last
 * frame into the first; one-shots hold the last frame. Missing slots give
 * the idle pose (frame 0). */
void hc_fp_sample(const HcFpWeapon* w, HcFpAnim anim, float frame, int loop, HcFpXf* out);
/* a = a..b by t (nlerp rotations). */
void hc_fp_blend(HcFpXf* a, const HcFpXf* b, float t, int count);
/* World transform per node and the skinning matrix (world * inverse bind). */
void hc_fp_pose(const HcFpWeapon* w, const HcFpXf* local, HcFpMtx* world, HcFpMtx* skin);
void hc_fp_skin_vertex(const HcFpMtx* skin, const HcFpVertex* v, float pos[3], float normal[3]);
void hc_fp_mtx_point(const HcFpMtx m, const float p[3], float out[3]);

#endif
