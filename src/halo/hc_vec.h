/* Small vector math used by the Halo sandbox. Halo convention: Z is up,
 * right-handed, angles in radians, yaw measured from +X toward +Y. */
#ifndef HC_VEC_H
#define HC_VEC_H

#include <math.h>

#define HC_PI 3.14159265358979f
#define HC_DEG2RAD(d) ((d) * (HC_PI / 180.0f))
#define HC_RAD2DEG(r) ((r) * (180.0f / HC_PI))

typedef struct hv3 {
    float x, y, z;
} hv3;

static inline hv3 hv3_make(float x, float y, float z) { hv3 r = { x, y, z }; return r; }
static inline hv3 hv3_add(hv3 a, hv3 b) { return hv3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline hv3 hv3_sub(hv3 a, hv3 b) { return hv3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline hv3 hv3_scale(hv3 a, float s) { return hv3_make(a.x * s, a.y * s, a.z * s); }
static inline hv3 hv3_mad(hv3 a, hv3 b, float s) { return hv3_make(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline float hv3_dot(hv3 a, hv3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline hv3 hv3_cross(hv3 a, hv3 b) {
    return hv3_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float hv3_len(hv3 a) { return sqrtf(hv3_dot(a, a)); }
static inline float hv3_len_xy(hv3 a) { return sqrtf(a.x * a.x + a.y * a.y); }
static inline float hv3_dist(hv3 a, hv3 b) { return hv3_len(hv3_sub(a, b)); }
static inline float hv3_dist_xy(hv3 a, hv3 b) { return hv3_len_xy(hv3_sub(a, b)); }
static inline hv3 hv3_norm(hv3 a) {
    float l = hv3_len(a);
    return l > 1e-6f ? hv3_scale(a, 1.0f / l) : hv3_make(0, 0, 0);
}
static inline hv3 hv3_lerp(hv3 a, hv3 b, float t) { return hv3_mad(a, hv3_sub(b, a), t); }

/* Forward vector for a yaw/pitch pair (pitch positive = looking up). */
static inline hv3 hv3_from_angles(float yaw, float pitch) {
    float cp = cosf(pitch);
    return hv3_make(cosf(yaw) * cp, sinf(yaw) * cp, sinf(pitch));
}

static inline float hc_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float hc_minf(float a, float b) { return a < b ? a : b; }
static inline float hc_maxf(float a, float b) { return a > b ? a : b; }

/* Moves `cur` toward `target` by at most `step`. */
static inline float hc_approachf(float cur, float target, float step) {
    if (cur < target) return hc_minf(cur + step, target);
    return hc_maxf(cur - step, target);
}

/* Wraps an angle into [-pi, pi). */
static inline float hc_wrap_angle(float a) {
    while (a >= HC_PI) a -= 2.0f * HC_PI;
    while (a < -HC_PI) a += 2.0f * HC_PI;
    return a;
}

#endif
