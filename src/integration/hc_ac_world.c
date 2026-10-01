#include "hc_ac_world.h"

#include <string.h>

#include "hc_world_scale.h"
#include "m_collision_bg.h"
#include "m_field_info.h"
#include "m_field_make.h"

/* LineCheck only builds collision for the 3x3 tiles around its start point,
 * so long rays are walked in segments shorter than one tile. */
#define RAY_STEP_AC 28.0f
#define WALL_PROBE_LIFT_AC 10.0f
#define STEP_HEIGHT_AC 12.0f   /* largest ground change between tiles on foot */

#define TILE_AC 40.0f
#define GRID_W (BLOCK_X_NUM * 16)
#define GRID_H (BLOCK_Z_NUM * 16)
#define ASTAR_MAX_EXPANSIONS 900
#define ASTAR_WINDOW 40        /* tiles from the start in each direction */

static HcAcWorldStats s_stats;

HcAcWorldStats* hc_ac_world_stats(void) {
    return &s_stats;
}

static xyz_t lerp_ac(xyz_t a, xyz_t b, float t) {
    xyz_t r = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    return r;
}

static float ground_y_ac(xyz_t p) {
    return mCoBG_GetBgY_AngleS_FromWpos(NULL, p, 0.0f);
}

static int is_water_ac(xyz_t p) {
    return mCoBG_CheckWaterAttribute(mCoBG_Wpos2Attribute(p, NULL));
}

static void ac_move_biped(void* ctx, hv3 from, hv3 to, float radius, float height, HaloMoveResult* out) {
    (void)ctx;
    (void)height;
    xyz_t a = hc_h2a_pos(from);
    xyz_t b = hc_h2a_pos(to);
    xyz_t rev = { 0.0f, 0.0f, 0.0f };
    mCoBG_Check_c chk;
    float dx = to.x - from.x, dy = to.y - from.y;
    s16 yaw = (dx * dx + dy * dy > 1e-8f) ? hc_h2a_yaw(atan2f(dy, dx)) : 0;

    mCoBG_VirtualBGCheck(&rev, &chk, &a, &b, yaw, FALSE, TRUE, hc_h2a_len(radius), 0.0f, TRUE, 1,
                         mCoBG_CHECK_TYPE_PLAYER);

    xyz_t res = { b.x + rev.x, b.y, b.z + rev.z };
    out->position = hc_a2h_pos(res);
    out->hit_wall = chk.result.hit_wall != 0 || chk.result.hit_attribute_wall != 0;
    out->has_ground = 1;
    out->ground_z = hc_a2h_len(ground_y_ac(res));
    out->ground_normal = hv3_make(0.0f, 0.0f, 1.0f);
    out->in_water = chk.result.is_in_water;
}

static int ac_raycast(void* ctx, hv3 from, hv3 to, HaloRayHit* out) {
    (void)ctx;
    xyz_t a = hc_h2a_pos(from);
    xyz_t b = hc_h2a_pos(to);
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    s_stats.rays++;
    if (len < 0.01f) return 0;
    int steps = (int)(len / RAY_STEP_AC) + 1;
    float t0 = 0.0f;
    xyz_t p0 = a;
    for (int i = 1; i <= steps; i++) {
        float t1 = (float)i / (float)steps;
        xyz_t p1 = lerp_ac(a, b, t1);

        /* Terrain: the heightfield (and column tops) under the segment end. */
        if (p1.y < ground_y_ac(p1)) {
            float lo = t0, hi = t1;
            for (int k = 0; k < 6; k++) {
                float mid = 0.5f * (lo + hi);
                xyz_t pm = lerp_ac(a, b, mid);
                if (pm.y < ground_y_ac(pm)) hi = mid;
                else lo = mid;
            }
            xyz_t hp = lerp_ac(a, b, hi);
            hp.y = ground_y_ac(hp);
            out->fraction = hi;
            out->point = hc_a2h_pos(hp);
            out->normal = hv3_make(0.0f, 0.0f, 1.0f);
            out->is_water = is_water_ac(hp);
            return 1;
        }

        /* Cliffs, fences, trees, buildings. */
        xyz_t rev;
        s_stats.line_checks++;
        if (mCoBG_LineCheck_RemoveFg(&rev, p0, p1, NULL, mCoBG_LINE_CHECK_WALL) & mCoBG_LINE_CHECK_WALL) {
            xyz_t hp = { p1.x + rev.x, p1.y + rev.y, p1.z + rev.z };
            float hx = hp.x - a.x, hy = hp.y - a.y, hz = hp.z - a.z;
            float f = sqrtf(hx * hx + hy * hy + hz * hz) / len;
            out->fraction = hc_clampf(f, 0.0f, 1.0f);
            out->point = hc_a2h_pos(hp);
            hv3 d = hv3_norm(hv3_sub(to, from));
            hv3 n = hv3_norm(hv3_make(-d.x, -d.y, 0.0f));
            out->normal = (hv3_len(n) > 0.5f) ? n : hv3_make(0.0f, 0.0f, 1.0f);
            out->is_water = 0;
            return 1;
        }
        p0 = p1;
        t0 = t1;
    }
    return 0;
}

static int ac_ground_height(void* ctx, hv3 pos, float* out_z) {
    (void)ctx;
    xyz_t p = hc_h2a_pos(pos);
    *out_z = hc_a2h_len(ground_y_ac(p));
    return 1;
}

/* Straight-line walkability between two AC positions at foot level. */
static int walkable_ac(xyz_t a, xyz_t b) {
    float dx = b.x - a.x, dz = b.z - a.z;
    float len = sqrtf(dx * dx + dz * dz);
    int steps = (int)(len / RAY_STEP_AC) + 1;
    xyz_t p0 = a;
    float y0 = ground_y_ac(a);
    for (int i = 1; i <= steps; i++) {
        xyz_t p1 = lerp_ac(a, b, (float)i / (float)steps);
        float y1 = ground_y_ac(p1);
        if (fabsf(y1 - y0) > STEP_HEIGHT_AC) return 0;
        if (is_water_ac(p1)) return 0;
        xyz_t q0 = { p0.x, y0 + WALL_PROBE_LIFT_AC, p0.z };
        xyz_t q1 = { p1.x, y1 + WALL_PROBE_LIFT_AC, p1.z };
        xyz_t rev;
        s_stats.line_checks++;
        if (mCoBG_LineCheck_RemoveFg(&rev, q0, q1, NULL, mCoBG_LINE_CHECK_WALL) & mCoBG_LINE_CHECK_WALL) return 0;
        p0 = p1;
        y0 = y1;
    }
    return 1;
}

static int ac_can_walk(void* ctx, hv3 from, hv3 to, float radius) {
    (void)ctx;
    (void)radius;
    return walkable_ac(hc_h2a_pos(from), hc_h2a_pos(to));
}

/* ---- A* over AC tiles with a lazily filled edge cache -------------------- */

/* Per tile: bits 0-7 = edge walkable (8 neighbours), bits 8-15 = edge known. */
static u16 s_edges[GRID_H][GRID_W];

static const int k_dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int k_dz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
static const int k_opposite[8] = { 1, 0, 3, 2, 7, 6, 5, 4 };

void hc_ac_world_invalidate(void) {
    memset(s_edges, 0, sizeof(s_edges));
}

static xyz_t tile_center(int tx, int tz) {
    xyz_t p = { (tx + 0.5f) * TILE_AC, 0.0f, (tz + 0.5f) * TILE_AC };
    p.y = ground_y_ac(p);
    return p;
}

static int in_grid(int tx, int tz) {
    return tx >= 0 && tz >= 0 && tx < GRID_W && tz < GRID_H;
}

static int edge_ok(int tx, int tz, int dir) {
    int nx = tx + k_dx[dir], nz = tz + k_dz[dir];
    if (!in_grid(nx, nz)) return 0;
    u16 e = s_edges[tz][tx];
    if (e & (0x100 << dir)) return (e >> dir) & 1;
    int ok;
    if (dir >= 4) {
        /* No corner cutting: both orthogonal steps must be clear. */
        int ox = (k_dx[dir] > 0) ? 0 : 1;
        int oz = (k_dz[dir] > 0) ? 2 : 3;
        ok = edge_ok(tx, tz, ox) && edge_ok(tx, tz, oz) && walkable_ac(tile_center(tx, tz), tile_center(nx, nz));
    } else {
        ok = walkable_ac(tile_center(tx, tz), tile_center(nx, nz));
    }
    s_edges[tz][tx] |= (u16)((0x100 << dir) | (ok << dir));
    s_edges[nz][nx] |= (u16)((0x100 << k_opposite[dir]) | (ok << k_opposite[dir]));
    return ok;
}

#define AW (ASTAR_WINDOW * 2 + 1)

typedef struct AStarNode {
    float g, f;
    short parent;   /* index into window, -1 none */
    u8 open, closed;
} AStarNode;

static AStarNode s_nodes[AW * AW];

static int ac_find_path(void* ctx, hv3 from, hv3 to, hv3* out_points, int max_points) {
    (void)ctx;
    xyz_t a = hc_h2a_pos(from), b = hc_h2a_pos(to);
    s_stats.paths++;

    if (walkable_ac(a, b)) {
        out_points[0] = to;
        return 1;
    }

    int sx = (int)(a.x / TILE_AC), sz = (int)(a.z / TILE_AC);
    int gx = (int)(b.x / TILE_AC), gz = (int)(b.z / TILE_AC);
    if (!in_grid(sx, sz) || !in_grid(gx, gz)) return 0;
    int ox = sx - ASTAR_WINDOW, oz = sz - ASTAR_WINDOW;
    if (gx < ox || gz < oz || gx >= ox + AW || gz >= oz + AW) {
        /* Goal outside the search window: head toward its projection on it. */
        gx = sx + (int)hc_clampf((float)(gx - sx), -ASTAR_WINDOW, ASTAR_WINDOW);
        gz = sz + (int)hc_clampf((float)(gz - sz), -ASTAR_WINDOW, ASTAR_WINDOW);
    }

    memset(s_nodes, 0, sizeof(s_nodes));
    int start = (sz - oz) * AW + (sx - ox);
    int goal = (gz - oz) * AW + (gx - ox);
    s_nodes[start].open = 1;
    s_nodes[start].parent = -1;
    s_nodes[start].f = hypotf((float)(gx - sx), (float)(gz - sz));

    int best = start;
    float best_h = s_nodes[start].f;
    int found = 0;
    for (int it = 0; it < ASTAR_MAX_EXPANSIONS; it++) {
        int cur = -1;
        float cur_f = 1e30f;
        for (int i = 0; i < AW * AW; i++) {
            if (s_nodes[i].open && s_nodes[i].f < cur_f) {
                cur_f = s_nodes[i].f;
                cur = i;
            }
        }
        if (cur < 0) break;
        s_stats.path_expansions++;
        if (cur == goal) {
            found = 1;
            best = cur;
            break;
        }
        s_nodes[cur].open = 0;
        s_nodes[cur].closed = 1;
        int cx = cur % AW + ox, cz = cur / AW + oz;
        for (int d = 0; d < 8; d++) {
            int nx = cx + k_dx[d], nz = cz + k_dz[d];
            int wx = nx - ox, wz = nz - oz;
            if (wx < 0 || wz < 0 || wx >= AW || wz >= AW) continue;
            int ni = wz * AW + wx;
            if (s_nodes[ni].closed) continue;
            if (!edge_ok(cx, cz, d)) continue;
            float g = s_nodes[cur].g + (d < 4 ? 1.0f : 1.41421f);
            if (!s_nodes[ni].open || g < s_nodes[ni].g) {
                float h = hypotf((float)(gx - nx), (float)(gz - nz));
                s_nodes[ni].g = g;
                s_nodes[ni].f = g + h;
                s_nodes[ni].parent = (short)cur;
                s_nodes[ni].open = 1;
                if (h < best_h) {
                    best_h = h;
                    best = ni;
                }
            }
        }
    }
    (void)found;
    if (best == start) return 0;

    /* Walk back, then emit forward with simple string pulling. */
    int chain[AW * 2];
    int n = 0;
    for (int i = best; i >= 0 && n < (int)(sizeof(chain) / sizeof(chain[0])); i = s_nodes[i].parent) chain[n++] = i;
    int count = 0;
    xyz_t anchor = a;
    int k = n - 1; /* chain[n-1] is the start tile */
    while (k > 0 && count < max_points) {
        int far = k - 1;
        /* Skip ahead while the straight line stays walkable (cap the probes). */
        for (int probe = 0; probe < 6 && far - 1 >= 0; probe++) {
            int cand = far - 1;
            xyz_t c = tile_center(chain[cand] % AW + ox, chain[cand] / AW + oz);
            if (!walkable_ac(anchor, c)) break;
            far = cand;
        }
        xyz_t p = tile_center(chain[far] % AW + ox, chain[far] / AW + oz);
        out_points[count++] = hc_a2h_pos(p);
        anchor = p;
        k = far;
    }
    if (best == goal && count < max_points && walkable_ac(anchor, b)) out_points[count++] = to;
    return count;
}

static int ac_find_cover(void* ctx, hv3 from, hv3 threat, float radius, hv3* out) {
    (void)ctx;
    float best_d = 1e30f;
    int found = 0;
    for (int ring = 1; ring <= 3; ring++) {
        float r = radius * (float)ring / 3.0f;
        for (int i = 0; i < 12; i++) {
            float ang = (float)i * (2.0f * HC_PI / 12.0f);
            hv3 c = hv3_make(from.x + cosf(ang) * r, from.y + sinf(ang) * r, from.z);
            float gz;
            ac_ground_height(NULL, c, &gz);
            c.z = gz;
            if (!ac_can_walk(NULL, from, c, 0.2f)) continue;
            HaloRayHit hit;
            hv3 head = hv3_make(c.x, c.y, c.z + 0.4f);
            if (!ac_raycast(NULL, threat, head, &hit)) continue;
            float d = hv3_dist(from, c);
            if (d < best_d) {
                best_d = d;
                *out = c;
                found = 1;
            }
        }
        if (found) return 1;
    }
    return 0;
}

static const HaloWorldApi k_ac_world = {
    NULL, ac_move_biped, ac_raycast, ac_ground_height, ac_can_walk, ac_find_path, ac_find_cover, NULL,
};

const HaloWorldApi* hc_ac_world_api(void) {
    return &k_ac_world;
}
