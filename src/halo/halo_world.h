/* The Halo sandbox's view of the world it is running inside (the spec's
 * INavigationWorld). Everything is in Halo world units, Z-up. The sandbox
 * never includes host headers; a host adapter fills this table in. */
#ifndef HALO_WORLD_H
#define HALO_WORLD_H

#include "hc_vec.h"

typedef struct HaloRayHit {
    float fraction;      /* 0..1 along the ray */
    hv3 point;
    hv3 normal;
    int is_water;
} HaloRayHit;

typedef struct HaloMoveResult {
    hv3 position;        /* resolved feet position */
    int hit_wall;
    int has_ground;      /* ground found under the resolved position */
    float ground_z;
    hv3 ground_normal;
    int in_water;
} HaloMoveResult;

typedef struct HaloWorldApi {
    void* ctx;

    /* Sweeps a vertical cylinder (feet at `from`) to `to`, sliding along
     * walls. Vertical motion is not resolved here except reporting the
     * ground height under the result; the biped integrator decides landing. */
    void (*move_biped)(void* ctx, hv3 from, hv3 to, float radius, float height, HaloMoveResult* out);

    /* Static-world ray test. Returns nonzero on hit. */
    int (*raycast)(void* ctx, hv3 from, hv3 to, HaloRayHit* out);

    /* Ground height near `pos` (searching down from slightly above it). */
    int (*ground_height)(void* ctx, hv3 pos, float* out_z);

    /* Straight-line walkability for a biped of `radius`. */
    int (*can_walk)(void* ctx, hv3 from, hv3 to, float radius);

    /* Writes up to `max_points` waypoints (excluding `from`, ending at or
     * near `to`). Returns the count, 0 if no path. May be NULL. */
    int (*find_path)(void* ctx, hv3 from, hv3 to, hv3* out_points, int max_points);

    /* Finds a nearby point hidden from `threat`. Returns nonzero on success.
     * May be NULL. */
    int (*find_cover)(void* ctx, hv3 from, hv3 threat, float search_radius, hv3* out);

    /* Path distance (falls back to straight-line when NULL). */
    float (*distance_to_target)(void* ctx, hv3 from, hv3 to);
} HaloWorldApi;

static inline float halo_world_distance(const HaloWorldApi* w, hv3 a, hv3 b) {
    if (w && w->distance_to_target) return w->distance_to_target(w->ctx, a, b);
    return hv3_dist(a, b);
}

#endif
