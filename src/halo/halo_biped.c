/* Biped locomotion after halocea biped_update_moving.c / biped_update_physics.c:
 * desired velocity = throttle x max speeds, velocity approaches it within an
 * acceleration budget (no separate friction), gravity when airborne, and
 * biped_jump.c raises upward speed to jump_velocity. */
#include "halo_internal.h"

#define CROUCH_BLEND_RATE 7.0f   /* 1/s */
#define GROUND_SNAP_DOWN 0.12f   /* stay glued to slopes/steps when walking down */
#define GROUND_SNAP_UP 0.20f     /* largest step-up taken without jumping */

void halo_biped_update(HaloSim* s, int ui) {
    HaloUnit* u = &s->units[ui];
    const HaloBipedDef* d = halo_unit_def(u);
    const HaloWorldApi* w = s->world;

    float crouch_target = (u->control.crouch && !u->dead) ? 1.0f : 0.0f;
    u->crouch_blend = hc_approachf(u->crouch_blend, crouch_target, CROUCH_BLEND_RATE * HALO_DT);
    u->crouching = u->control.crouch;

    float tf = hc_clampf(u->control.throttle_forward, -1.0f, 1.0f);
    float tl = hc_clampf(u->control.throttle_left, -1.0f, 1.0f);
    float tmag = sqrtf(tf * tf + tl * tl);
    if (tmag > 1.0f) {
        tf /= tmag;
        tl /= tmag;
    }

    float forward_speed = tf >= 0.0f ? d->run_forward_speed : d->run_backward_speed;
    float speed_scale = 1.0f + (d->crouch_speed_scale - 1.0f) * u->crouch_blend;
    float cy = cosf(u->yaw), sy = sinf(u->yaw);
    float lf = tf * forward_speed * speed_scale;
    float ll = tl * d->run_sideways_speed * speed_scale;
    float want_x = cy * lf - sy * ll;
    float want_y = sy * lf + cy * ll;

    float accel = (u->grounded ? d->run_acceleration : d->airborne_acceleration) * HALO_DT;
    float dvx = want_x - u->vel.x;
    float dvy = want_y - u->vel.y;
    float dvl = sqrtf(dvx * dvx + dvy * dvy);
    if (!u->grounded) {
        /* In the air only steer; never brake below the launch speed. */
        if (tmag < 0.01f) dvl = 0.0f;
    }
    if (dvl > accel && dvl > 0.0f) {
        dvx *= accel / dvl;
        dvy *= accel / dvl;
    } else if (dvl == 0.0f) {
        dvx = dvy = 0.0f;
    }
    u->vel.x += dvx;
    u->vel.y += dvy;

    if (u->grounded && u->control.jump_pressed && !u->dead) {
        if (u->vel.z < d->jump_velocity) u->vel.z = d->jump_velocity;
        u->grounded = 0;
    }
    if (!u->grounded) u->vel.z -= HALO_GRAVITY * HALO_DT;

    hv3 from = u->pos;
    hv3 to = hv3_mad(from, u->vel, HALO_DT);

    HaloMoveResult r;
    if (w && w->move_biped) {
        w->move_biped(w->ctx, from, to, d->collision_radius, halo_unit_height(u), &r);
    } else {
        r.position = to;
        r.hit_wall = 0;
        r.has_ground = 1;
        r.ground_z = 0.0f;
        r.ground_normal = hv3_make(0, 0, 1);
        r.in_water = 0;
    }

    hv3 np = hv3_make(r.position.x, r.position.y, to.z);
    if (r.hit_wall) {
        /* Keep only the motion the world allowed. */
        u->vel.x = (np.x - from.x) / HALO_DT;
        u->vel.y = (np.y - from.y) / HALO_DT;
    }

    if (u->grounded) {
        if (r.has_ground && r.ground_z <= from.z + GROUND_SNAP_UP && r.ground_z >= from.z - GROUND_SNAP_DOWN) {
            np.z = r.ground_z;
            u->vel.z = 0.0f;
        } else if (r.has_ground && r.ground_z > from.z + GROUND_SNAP_UP) {
            /* Too tall to step onto: treat as a wall. */
            np.x = from.x;
            np.y = from.y;
            np.z = from.z;
            u->vel.x = u->vel.y = 0.0f;
        } else {
            u->grounded = 0;
            np.z = from.z;
        }
    } else if (r.has_ground && np.z <= r.ground_z && u->vel.z <= 0.0f) {
        np.z = r.ground_z;
        u->vel.z = 0.0f;
        u->grounded = 1;
    } else if (r.has_ground && np.z < r.ground_z) {
        /* Rising through a slope: lift onto it. */
        np.z = r.ground_z;
    }

    if (u->grounded && u->dead) {
        u->vel.x *= 0.8f;
        u->vel.y *= 0.8f;
    }

    float moved = hv3_dist_xy(np, from);
    u->move_anim += moved * 6.0f;
    u->pos = np;
}
