#include "hc_draw.h"

#include <stdio.h>
#include <string.h>

#include "graph.h"
#include "hc_biped_view.h"
#include "hc_fp_view.h"
#include "hc_gfx.h"
#include "hc_hud_view.h"
#include "hc_models.h"
#include "hc_world_scale.h"
#include "m_font.h"
#include "m_lib.h"
#include "pc_text_draw.h"
#include "sys_matrix.h"

#define S HALO_TO_AC_SCALE
#define VM_SCALE 0.25f        /* viewmodel is drawn small and close so it never clips walls */
#define HUD_BLUE 0x4FB4FFFF
#define HUD_BLUE_DIM 0x123A6090
#define HUD_RED 0xFF3A2AFF
/* ---- effects --------------------------------------------------------------- */

typedef struct HcFx {
    int active;
    xyz_t pos;
    float age, life;
    float size0, size1;   /* AC units; beam width for beams */
    u32 color;
    int beam;             /* draw a ribbon pos -> pos2 instead of a glow */
    xyz_t pos2;
    float rise;           /* AC units/s upward drift (smoke) */
    xyz_t vel;            /* AC units/s, pulled down by `gravity` */
    float gravity;
} HcFx;

#define HC_MAX_FX 160
#define DRAW_RANGE_AC 1900.0f  /* beyond the town fog; don't spend display list on it */
static HcFx s_fx[HC_MAX_FX];

static HcFx* fx_spawn(xyz_t pos, float life, float size0, float size1, u32 color) {
    int slot = 0;
    float oldest = -1.0f;
    for (int i = 0; i < HC_MAX_FX; i++) {
        if (!s_fx[i].active) {
            slot = i;
            break;
        }
        if (s_fx[i].age > oldest) {
            oldest = s_fx[i].age;
            slot = i;
        }
    }
    HcFx* f = &s_fx[slot];
    memset(f, 0, sizeof(*f));
    f->active = 1;
    f->pos = pos;
    f->life = life;
    f->size0 = size0;
    f->size1 = size1;
    f->color = color;
    return f;
}

void hc_fx_clear(void) {
    memset(s_fx, 0, sizeof(s_fx));
}

void hc_fx_update(float dt) {
    for (int i = 0; i < HC_MAX_FX; i++) {
        if (!s_fx[i].active) continue;
        HcFx* f = &s_fx[i];
        f->age += dt;
        f->vel.y -= f->gravity * dt;
        f->pos.x += f->vel.x * dt;
        f->pos.y += (f->vel.y + f->rise) * dt;
        f->pos.z += f->vel.z * dt;
        if (f->age >= f->life) f->active = 0;
    }
}

void hc_fx_bells(xyz_t pos) {
    static unsigned int seed = 0x1234567u;
    pos.y += 20.0f;
    for (int i = 0; i < 28; i++) {
        seed = seed * 1103515245u + 12345u;
        float a = (float)((seed >> 8) & 0xFFFF) / 65536.0f * 2.0f * HC_PI;
        float k = 0.4f + (float)((seed >> 20) & 0xFF) / 255.0f * 0.6f;
        HcFx* f = fx_spawn(pos, 1.6f + k * 0.6f, 3.5f, 2.0f, i % 3 ? 0xFFD040FF : 0xFFF4B0FF);
        f->vel.x = cosf(a) * 90.0f * k;
        f->vel.z = sinf(a) * 90.0f * k;
        f->vel.y = 140.0f + 90.0f * k;
        f->gravity = 330.0f;
    }
    fx_spawn(pos, 0.5f, 10.0f, 60.0f, 0xFFE070C0);
}

static void explosion_fx(xyz_t p, int def, float radius) {
    u32 outer = 0xFFB050E0, core = 0xFFF4C8F0;
    switch (def) {
        case HALO_PROJ_PLASMA_GRENADE: outer = 0x9CD2FFE0; core = 0xFFFFFFF0; break;
        case HALO_PROJ_FUEL_ROD: outer = 0x7CFF3CE0; core = 0xE8FFD0F0; break;
        case HALO_PROJ_NEEDLE: outer = 0xFF5AD2E0; core = 0xFFE0F6F0; break;
        default: break;
    }
    if (radius <= 0.0f) {
        /* A single needle popping. */
        fx_spawn(p, 0.2f, 4.0f, hc_h2a_len(0.25f), outer);
        return;
    }
    fx_spawn(p, 0.5f, hc_h2a_len(0.2f), hc_h2a_len(radius * 0.8f), outer);
    fx_spawn(p, 0.28f, hc_h2a_len(0.1f), hc_h2a_len(radius * 0.45f), core);
    if (def == HALO_PROJ_ROCKET || def == HALO_PROJ_FRAG_GRENADE) {
        for (int k = 0; k < 3; k++) {
            xyz_t s = p;
            s.x += (float)(k - 1) * hc_h2a_len(0.25f);
            HcFx* f = fx_spawn(s, 1.4f, hc_h2a_len(0.3f), hc_h2a_len(radius * 0.6f), 0x6A665E90);
            f->rise = 18.0f;
        }
    }
}

void hc_fx_from_event(HaloSim* sim, const HaloEvent* e) {
    xyz_t p = hc_h2a_pos(e->pos);
    switch (e->type) {
        case HALO_EV_PROJECTILE_IMPACT: {
            const HaloProjectileDef* pd = &g_halo_projectiles[e->def];
            u32 c = (pd->render_rgba & 0xFFFFFF00) | 0xE0;
            float sz = pd->render_style == HALO_RENDER_TRACER ? 3.0f : 6.0f;
            if (e->def == HALO_PROJ_SNIPER_BULLET) sz = 8.0f;
            if (pd->render_style == HALO_RENDER_FLAME) sz = 10.0f;
            fx_spawn(p, 0.12f, sz, sz * 1.8f, c);
            break;
        }
        case HALO_EV_EXPLOSION:
            explosion_fx(p, e->def, e->value);
            break;
        case HALO_EV_WEAPON_FIRED: {
            if (e->def == HALO_WEAPON_SNIPER_RIFLE && sim->world && sim->world->raycast) {
                /* CE's sniper leaves a vapour trail hanging in the air. */
                hv3 from = hv3_mad(e->pos, e->dir, 0.35f);
                from.z -= 0.04f;
                hv3 to = hv3_mad(e->pos, e->dir, 120.0f);
                HaloRayHit hit;
                if (sim->world->raycast(sim->world->ctx, e->pos, to, &hit)) to = hit.point;
                HcFx* f = fx_spawn(hc_h2a_pos(from), 0.7f, 2.2f, 0.5f, 0xD8F0FFB0);
                f->beam = 1;
                f->pos2 = hc_h2a_pos(to);
            }
            if (e->unit >= 0 && !sim->units[e->unit].is_player && e->def >= 0) {
                hv3 m = hv3_mad(e->pos, e->dir, 0.2f);
                u32 c = (hc_weapon_glow_rgba((HaloWeaponId)e->def, 0x9CFF8AFF) & 0xFFFFFF00) | 0xD0;
                fx_spawn(hc_h2a_pos(m), 0.08f, 4.0f, 6.0f, c);
            }
            break;
        }
        case HALO_EV_SHIELD_DEPLETED:
            fx_spawn(p, 0.3f, hc_h2a_len(0.15f), hc_h2a_len(0.5f), 0x7FC8FFB0);
            break;
        default:
            break;
    }
}

/* ---- units ----------------------------------------------------------------- */

static float lerp_angle(float a, float b, float t) {
    return a + hc_wrap_angle(b - a) * t;
}

static u32 tint_flash(u32 c, float flash) {
    return flash > 0.0f ? hc_rgba_lerp(c, (c & 0xFF) | 0xFFFFFF00, hc_clampf(flash, 0.0f, 1.0f) * 0.7f) : c;
}

static Gfx* draw_model_boxes(Gfx* g, GRAPH* graph, const HaloSim* sim, int ui, int hull, float hull_alpha) {
    const HaloUnit* u = &sim->units[ui];
    const HcModel* m = hc_model_for_biped(u->biped);
    int fleeing = sim->ai[ui].active && sim->ai[ui].state == HALO_AI_FLEE;
    float t = sim->time;

    float speed = hv3_len_xy(u->vel);
    float swing = (u->grounded && speed > 0.1f && !u->dead) ? sinf(u->move_anim) * hc_clampf(speed, 0.0f, 1.2f) * 0.55f
                                                             : 0.0f;
    float arm_pitch = -1.05f;
    if (fleeing) arm_pitch = -2.7f + sinf(t * 17.0f + (float)ui) * 0.45f;
    if (u->dead) arm_pitch = -0.4f;

    for (int i = 0; i < m->count; i++) {
        const HcBox* b = &m->boxes[i];
        if (fleeing && (b->part == HC_PART_WEAPON || b->part == HC_PART_GLOW)) continue;
        if (u->dead && (b->part == HC_PART_GLOW)) continue;
        u32 c = b->rgba;
        if (b->part == HC_PART_WEAPON) c = hc_weapon_body_rgba(u->weapon.id, c);
        if (b->part == HC_PART_GLOW) c = hc_weapon_glow_rgba(u->weapon.id, c);
        if (b->part == HC_PART_GLOW && u->weapon.fire_flash > 0.0f) c = hc_rgba_lerp(c, 0xFFFFFFFF, u->weapon.fire_flash);
        c = tint_flash(c, u->hurt_flash);
        float inflate = 0.0f;
        if (hull) {
            c = (0x9CD8FF00) | (u32)(hull_alpha * 255.0f);
            inflate = 0.012f;
        }

        float rot = 0.0f, pivot = 0.0f, side_swing = 0.0f;
        switch (b->part) {
            case HC_PART_LEG_L: rot = swing; pivot = m->hip_u; break;
            case HC_PART_LEG_R: rot = -swing; pivot = m->hip_u; break;
            case HC_PART_ARM_L:
                rot = arm_pitch + (fleeing ? 0.6f * sinf(t * 23.0f) : 0.0f);
                pivot = m->shoulder_u;
                break;
            case HC_PART_ARM_R:
                rot = arm_pitch - (fleeing ? 0.6f * sinf(t * 23.0f) : 0.0f);
                pivot = m->shoulder_u;
                side_swing = 0.0f;
                break;
            default: break;
        }
        (void)side_swing;
        float cx = b->l * S, cy = b->u * S, cz = b->f * S;
        float hx = (b->hl + inflate) * S, hy = (b->hu + inflate) * S, hz = (b->hf + inflate) * S;
        if (rot != 0.0f) {
            Matrix_push();
            Matrix_translate(0.0f, pivot * S, 0.0f, MTX_MULT);
            Matrix_RotateX(HC_RAD_TO_S16(rot), MTX_MULT);
            Matrix_translate(0.0f, -pivot * S, 0.0f, MTX_MULT);
            g = hc_gfx_box(g, graph, cx, cy, cz, hx, hy, hz, c);
            Matrix_pull();
        } else {
            g = hc_gfx_box(g, graph, cx, cy, cz, hx, hy, hz, c);
        }
    }
    return g;
}

static void unit_matrix(const HaloSim* sim, int ui) {
    const HaloUnit* u = &sim->units[ui];
    hv3 pos = halo_unit_pos_interp(u, sim->alpha);
    float yaw = lerp_angle(u->prev_yaw, u->yaw, sim->alpha);
    xyz_t ap = hc_h2a_pos(pos);
    Matrix_translate(ap.x, ap.y, ap.z, MTX_LOAD);
    Matrix_RotateY(hc_h2a_yaw(yaw), MTX_MULT);
    if (u->dead) {
        float fall = hc_clampf(u->dead_time / 0.35f, 0.0f, 1.0f);
        Matrix_RotateX(HC_RAD_TO_S16(-fall * 1.5f), MTX_MULT);
    } else if (u->crouch_blend > 0.0f) {
        Matrix_scale(1.0f, 1.0f - u->crouch_blend * 0.3f, 1.0f, MTX_MULT);
    }
}

static Gfx* draw_unit(Gfx* g, GRAPH* graph, const HaloSim* sim, int ui) {
    unit_matrix(sim, ui);
    return draw_model_boxes(g, graph, sim, ui, 0, 0.0f);
}

/* ---- first-person weapon --------------------------------------------------- */

static hv3 view_basis_point(const HcView* v, float f, float l, float u) {
    float cy = cosf(v->yaw), sy = sinf(v->yaw), cp = cosf(v->pitch), sp = sinf(v->pitch);
    hv3 fwd = hv3_make(cy * cp, sy * cp, sp);
    hv3 left = hv3_make(-sy, cy, 0.0f);
    hv3 up = hv3_cross(fwd, left);
    hv3 r = hv3_scale(fwd, f);
    r = hv3_mad(r, left, l);
    r = hv3_mad(r, up, u);
    return r;
}

static Gfx* draw_viewmodel(Gfx* g, GRAPH* graph, GAME_PLAY* play, const HaloSim* sim, const HcView* v,
                           const HaloUnit* p) {
    const HaloWeaponState* w = &p->weapon;
    if (hc_fp_view_available(w->id)) return hc_fp_view_draw_opa(g, graph, play, v, p);
    const HcModel* m = hc_model_first_person(w->id);
    if (m == NULL) return g;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];

    float bob_l = sinf(v->bob_phase) * 0.006f * v->bob_amount;
    float bob_u = -fabsf(cosf(v->bob_phase)) * 0.005f * v->bob_amount;
    float kick = w->recoil;
    float off_f = -kick * 0.025f;
    float off_u = bob_u + kick * 0.004f;
    if (v->zoom > 1.01f) return g; /* looking down the scope */
    float dip = 0.0f;
    if (w->reload_timer > 0.0f && wd->reload_time > 0.0f) {
        float prog = 1.0f - w->reload_timer / wd->reload_time;
        dip = wd->reload_per_round ? 0.05f + sinf(prog * HC_PI) * 0.015f : sinf(prog * HC_PI) * 0.09f;
    }
    if (w->ready_timer > 0.0f && wd->ready_time > 0.0f) dip = hc_maxf(dip, (w->ready_timer / wd->ready_time) * 0.12f);
    if (p->grenade_timer > 0.0f) dip = hc_maxf(dip, 0.05f);
    off_u -= dip;

    Matrix_translate(v->eye.x, v->eye.y, v->eye.z, MTX_LOAD);
    Matrix_RotateY(hc_h2a_yaw(v->yaw), MTX_MULT);
    Matrix_RotateX(HC_RAD_TO_S16(-v->pitch), MTX_MULT);

    float k = S * VM_SCALE;
    int glow_index = 0;
    for (int i = 0; i < m->count; i++) {
        const HcBox* b = &m->boxes[i];
        u32 c = b->rgba;
        if (b->part == HC_PART_GLOW) {
            int gi = glow_index++;
            if (wd->rounds_loaded_maximum <= 0) {
                c = hc_rgba_lerp(b->rgba, 0xFF4A2AFF, w->heat);
                if (w->charge > 0.2f) c = hc_rgba_lerp(c, 0xFFFFFFFF, hc_clampf(w->charge / 0.9f, 0.0f, 1.0f));
                if (w->overheated) c = (sinf(v->time * 30.0f) > 0.0f) ? 0xFF6A2AFF : 0xC03010FF;
            } else if (w->id == HALO_WEAPON_NEEDLER) {
                /* One crystal per five needles left in the magazine. */
                if (w->rounds_loaded <= gi * 5) continue;
            } else if (w->rounds_loaded <= hc_maxf(1.0f, wd->rounds_loaded_maximum / 6.0f)) {
                c = 0xFF5A3AFF;
            }
            if (w->fire_flash > 0.0f) c = hc_rgba_lerp(c, 0xFFFFFFFF, w->fire_flash * 0.5f);
        }
        float f = b->f + off_f, l = b->l + bob_l, u = b->u + off_u;
        g = hc_gfx_box(g, graph, l * k, u * k, f * k, b->hl * k, b->hu * k, b->hf * k, c);
    }
    return g;
}

/* ---- world pass ------------------------------------------------------------ */

static Gfx* draw_projectiles(Gfx* g, GRAPH* graph, GAME_PLAY* play, const HaloSim* sim, const HcView* v) {
    for (int i = 0; i < HALO_MAX_PROJECTILES; i++) {
        const HaloProjectile* p = &sim->projectiles[i];
        if (!p->active) continue;
        const HaloProjectileDef* pd = &g_halo_projectiles[p->def];
        hv3 hp = hv3_add(hv3_lerp(p->prev_pos, p->pos, sim->alpha), hv3_scale(p->visual_offset, VM_SCALE));
        xyz_t ap = hc_h2a_pos(hp);
        float dx = ap.x - v->eye.x, dy = ap.y - v->eye.y, dz = ap.z - v->eye.z;
        if (dx * dx + dy * dy + dz * dz > DRAW_RANGE_AC * DRAW_RANGE_AC) continue;
        float r = hc_h2a_len(pd->render_size);
        u32 c = pd->render_rgba;
        float speed = hv3_len(p->vel);
        switch (pd->render_style) {
            case HALO_RENDER_TRACER: {
                float trail = hc_minf(p->distance + 0.3f, p->def == HALO_PROJ_SHOTGUN_PELLET ? 0.6f : 1.6f);
                hv3 tail = speed > 0.0f ? hv3_mad(hp, p->vel, -trail / speed) : hp;
                float width = p->def == HALO_PROJ_SHOTGUN_PELLET ? 0.9f : 1.4f;
                g = hc_gfx_beam(g, graph, hc_h2a_pos(tail), ap, v->eye, width, (c & 0xFFFFFF00), (c & 0xFFFFFF00) | 0xDC);
                break;
            }
            case HALO_RENDER_ROCKET: {
                hv3 tail = speed > 0.0f ? hv3_mad(hp, p->vel, -hc_minf(p->distance + 0.1f, 1.4f) / speed) : hp;
                g = hc_gfx_beam(g, graph, hc_h2a_pos(tail), ap, v->eye, hc_h2a_len(0.12f), 0xB4B0A800, 0xD8D4CCA0);
                g = hc_gfx_glow(g, graph, play, ap, r * 2.0f, 0xFFB040C0);
                g = hc_gfx_glow(g, graph, play, ap, r * 0.8f, 0xFFF8E0FF);
                break;
            }
            case HALO_RENDER_FLAME: {
                float life = pd->maximum_range / hc_maxf(pd->initial_velocity, 0.1f);
                float t = hc_clampf(p->age / life, 0.0f, 1.0f);
                u32 fc = hc_rgba_lerp(0xFFD060FF, 0xC02A10FF, t);
                g = hc_gfx_glow(g, graph, play, ap, r * (1.0f + t * 4.0f), (fc & 0xFFFFFF00) | (u32)(200.0f * (1.0f - t * 0.7f)));
                if (t < 0.4f) g = hc_gfx_glow(g, graph, play, ap, r * 0.8f, 0xFFF4C0C0);
                break;
            }
            case HALO_RENDER_NEEDLE: {
                if (p->attached_unit < 0 && speed > 0.0f) {
                    hv3 tail = hv3_mad(hp, p->vel, -hc_minf(p->distance + 0.05f, 0.35f) / speed);
                    g = hc_gfx_beam(g, graph, hc_h2a_pos(tail), ap, v->eye, 1.6f, (c & 0xFFFFFF00), (c & 0xFFFFFF00) | 0xE0);
                }
                float pulse = p->attached_unit >= 0 ? 0.6f + 0.4f * sinf(v->time * 20.0f + (float)i) : 1.0f;
                g = hc_gfx_glow(g, graph, play, ap, r * 2.0f * pulse, (c & 0xFFFFFF00) | 0xA0);
                break;
            }
            case HALO_RENDER_GRENADE: {
                int armed = p->attached_unit >= 0 || p->stuck;
                if (p->def == HALO_PROJ_FRAG_GRENADE) {
                    float blink = sinf(v->time * 16.0f) > 0.0f ? 1.0f : 0.3f;
                    g = hc_gfx_glow(g, graph, play, ap, r * 1.4f, 0x4A5A30F0);
                    g = hc_gfx_glow(g, graph, play, ap, r * 0.5f, 0xFF4030FF & (0xFFFFFF00 | (u32)(blink * 255.0f)));
                } else {
                    float pulse = armed ? 0.5f + 0.5f * sinf(v->time * 28.0f) : 0.3f;
                    g = hc_gfx_glow(g, graph, play, ap, r * (1.6f + pulse), (c & 0xFFFFFF00) | 0x90);
                    g = hc_gfx_glow(g, graph, play, ap, r * 0.8f, 0xE8F4FFFF);
                }
                break;
            }
            default: {
                if (speed > 0.0f) {
                    hv3 tail = hv3_mad(hp, p->vel, -hc_minf(p->distance + 0.05f, 0.4f) / speed);
                    g = hc_gfx_beam(g, graph, hc_h2a_pos(tail), ap, v->eye, r * 1.2f, (c & 0xFFFFFF00), (c & 0xFFFFFF00) | 0x70);
                }
                g = hc_gfx_glow(g, graph, play, ap, r * 2.2f, (c & 0xFFFFFF00) | 0x80);
                g = hc_gfx_glow(g, graph, play, ap, r * 0.9f, 0xF0FFE8F0);
                break;
            }
        }
    }
    return g;
}

static Gfx* draw_fx(Gfx* g, GRAPH* graph, GAME_PLAY* play, const HcView* v) {
    for (int i = 0; i < HC_MAX_FX; i++) {
        const HcFx* f = &s_fx[i];
        if (!f->active) continue;
        float t = f->age / f->life;
        float size = f->size0 + (f->size1 - f->size0) * t;
        u32 a = (u32)((float)HC_A(f->color) * (1.0f - t));
        if (f->beam) {
            u32 c = (f->color & 0xFFFFFF00) | a;
            g = hc_gfx_beam(g, graph, f->pos, f->pos2, v->eye, size, c, c);
            continue;
        }
        g = hc_gfx_glow(g, graph, play, f->pos, size, (f->color & 0xFFFFFF00) | a);
    }
    return g;
}

static Gfx* draw_debug_volumes(Gfx* g, GRAPH* graph, const HaloSim* sim) {
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        if (!u->active || u->dead) continue;
        const HaloBipedDef* d = halo_unit_def(u);
        float h = halo_unit_height(u);
        xyz_t ap = hc_h2a_pos(halo_unit_pos_interp(u, sim->alpha));
        Matrix_translate(ap.x, ap.y, ap.z, MTX_LOAD);
        float r = hc_h2a_len(d->collision_radius);
        g = hc_gfx_box(g, graph, 0.0f, hc_h2a_len(h) * 0.5f, 0.0f, r, hc_h2a_len(h) * 0.5f, r,
                       u->team == HALO_TEAM_HUMAN ? 0x40FF4050 : 0xFF404050);
    }
    return g;
}

static Gfx* draw_debug_paths(Gfx* g, GRAPH* graph, const HaloSim* sim) {
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloAi* a = &sim->ai[i];
        if (!a->active || !sim->units[i].active || sim->units[i].dead) continue;
        for (int k = a->path_index; k < a->path_count; k++) {
            xyz_t p = hc_h2a_pos(a->path[k]);
            Matrix_translate(p.x, p.y, p.z, MTX_LOAD);
            g = hc_gfx_box(g, graph, 0.0f, 2.0f, 0.0f, 2.0f, 2.0f, 2.0f, k == a->path_index ? 0xFFFF40FF : 0xFFA040FF);
        }
        if (a->has_move_goal) {
            xyz_t p = hc_h2a_pos(a->move_goal);
            Matrix_translate(p.x, p.y, p.z, MTX_LOAD);
            g = hc_gfx_box(g, graph, 0.0f, 4.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0x40FFFFFF);
        }
    }
    return g;
}

void hc_draw_world(GAME_PLAY* play, HaloSim* sim, const HcView* v) {
    GRAPH* graph = play->game.graph;
    HcGfxRedirect r;
    hc_gfx_begin(graph, &r, HC_GFX_OPA | HC_GFX_XLU);
    OPEN_DISP(graph);

    Gfx* opa = NOW_POLY_OPA_DISP;
    Gfx* xlu = NOW_POLY_XLU_DISP;
    opa = hc_gfx_mode_opa(opa);
    xlu = hc_gfx_mode_xlu(xlu);
    hc_biped_view_begin_frame();
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        /* The AC villager stands in for the Chief in AC camera mode; kinematic
         * proxies are AC actors that the host already draws. */
        if (!u->active || u->is_player || u->kinematic) continue;
        xyz_t ap = hc_h2a_pos(u->pos);
        float dx = ap.x - v->eye.x, dz = ap.z - v->eye.z;
        if (dx * dx + dz * dz > DRAW_RANGE_AC * DRAW_RANGE_AC) continue;
        if (!hc_biped_view_draw(&opa, &xlu, graph, play, sim, i, v)) opa = draw_unit(opa, graph, sim, i);
    }
    if (v->show_nav) opa = draw_debug_paths(opa, graph, sim);
    HaloUnit* p = halo_player(sim);
    if (v->first_person && p && !p->dead && p->weapon.id != HALO_WEAPON_NONE) opa = draw_viewmodel(opa, graph, play, sim, v, p);
    SET_POLY_OPA_DISP(opa);

    xlu = draw_projectiles(xlu, graph, play, sim, v);
    xlu = draw_fx(xlu, graph, play, v);
    if (v->first_person && p && !p->dead && hc_fp_view_available(p->weapon.id))
        xlu = hc_fp_view_draw_xlu(xlu, graph, v, p);
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        if (!u->active || u->dead || u->is_player || u->kinematic || u->shield_flash <= 0.0f) continue;
        if (hc_biped_view_available(u)) continue;
        unit_matrix(sim, i);
        xlu = draw_model_boxes(xlu, graph, sim, i, 1, u->shield_flash * 0.55f);
    }
    if (v->first_person && p && !p->dead && p->weapon.fire_flash > 0.5f && p->weapon.id != HALO_WEAPON_NONE &&
        v->zoom <= 1.01f) {
        const HaloWeaponDef* wd = &g_halo_weapons[p->weapon.id];
        const HcModel* fm = hc_model_first_person(p->weapon.id);
        /* Muzzle = the front of the weapon's furthest-forward box. */
        float front = 0.28f, muzzle_u = -0.068f, muzzle_l = wd->fp_offset[1] * 1.1f;
        for (int k = 0; fm && k < fm->count; k++) {
            const HcBox* b = &fm->boxes[k];
            if (b->part == HC_PART_WEAPON && b->f + b->hf > front) {
                front = b->f + b->hf;
                muzzle_u = b->u;
                muzzle_l = b->l;
            }
        }
        float fp[3];
        float vm = VM_SCALE;
        if (hc_fp_view_muzzle(p, fp)) {
            front = fp[0];
            muzzle_l = fp[1];
            muzzle_u = fp[2];
            vm = HC_FP_VIEW_SCALE;
        }
        hv3 eye = hc_a2h_pos(v->eye);
        hv3 m = hv3_add(eye, view_basis_point(v, front * vm, muzzle_l * vm, muzzle_u * vm));
        u32 c = (hc_weapon_glow_rgba(p->weapon.id, 0xFFD27AFF) & 0xFFFFFF00) | 0xF0;
        float big = (p->weapon.id == HALO_WEAPON_ROCKET_LAUNCHER || p->weapon.id == HALO_WEAPON_SHOTGUN ||
                     p->weapon.id == HALO_WEAPON_SNIPER_RIFLE || p->weapon.id == HALO_WEAPON_FUEL_ROD)
                        ? 1.8f
                        : 1.0f;
        float radius = S * VM_SCALE * (0.045f + 0.035f * p->weapon.fire_flash) * big;
        xlu = hc_gfx_glow(xlu, graph, play, hc_h2a_pos(m), radius, c);
    }
    if (v->show_collision) xlu = draw_debug_volumes(xlu, graph, sim);
    SET_POLY_XLU_DISP(xlu);

    CLOSE_DISP(graph);
    hc_gfx_end(graph, &r);
}

/* ---- sky ------------------------------------------------------------------- */

void hc_draw_sky(GAME_PLAY* play, const HcView* v, float fov_y_deg) {
    static Gfx s_sky[2][160];
    static Vp s_vp[2];
    static int s_flip;
    GRAPH* graph = play->game.graph;
    s_flip ^= 1;
    Gfx* start = s_sky[s_flip];
    Gfx* g = start;

    /* This runs before showView sets the frame's viewport, and rectangles
     * drawn without one land nowhere; reuse last frame's. */
    s_vp[s_flip] = play->view.viewport;
    gSPViewport(g++, &s_vp[s_flip]);

    const u8* fc = play->global_light.fogColor;
    u32 horizon = HC_RGBA(fc[0], fc[1], fc[2], 255);
    u32 zenith = HC_RGBA(fc[0] * 0.35f + 20.0f, fc[1] * 0.5f + 40.0f, hc_minf(255.0f, fc[2] * 0.7f + 90.0f), 255);
    float focal = 120.0f / tanf(HC_DEG2RAD(fov_y_deg) * 0.5f); /* HUD px per unit tan */
    float yh = 120.0f + tanf(hc_clampf(v->pitch, -1.5f, 1.5f)) * focal;

    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, 640, 480);
    g = hc_gfx_hud_mode(g);
    float sky_bottom = hc_clampf(yh, 0.0f, 240.0f);
    const int bands = 20;
    for (int i = 0; i < bands && sky_bottom > 0.0f; i++) {
        float y0 = sky_bottom * i / bands, y1 = sky_bottom * (i + 1) / bands;
        float elev = v->pitch + atanf((120.0f - (y0 + y1) * 0.5f) / focal);
        float t = sqrtf(hc_clampf(elev / 1.2f, 0.0f, 1.0f));
        g = hc_gfx_hud_rect(g, 0.0f, y0, 320.0f, y1 - y0 + 1.0f, hc_rgba_lerp(horizon, zenith, t));
    }
    if (sky_bottom < 240.0f) g = hc_gfx_hud_rect(g, 0.0f, sky_bottom, 320.0f, 240.0f - sky_bottom, horizon);
    gSPEndDisplayList(g++);

    OPEN_DISP(graph);
    gSPDisplayList(NOW_BG_OPA_DISP++, start);
    CLOSE_DISP(graph);
}

/* ---- HUD ------------------------------------------------------------------- */

static int project(GAME_PLAY* play, xyz_t w, float* sx, float* sy) {
    MtxF* m = &play->projection_matrix;
    float cw = m->ww + m->wx * w.x + m->wy * w.y + m->wz * w.z;
    if (cw <= 1.0f) return 0;
    xyz_t s;
    Game_play_Projection_Trans(play, &w, &s);
    *sx = s.x;
    *sy = s.y;
    return 1;
}

static Gfx* hud_shield(Gfx* g, const HaloUnit* p, float t) {
    const HaloBipedDef* d = halo_unit_def(p);
    float x = 210.0f, y = 14.0f, w = 96.0f, h = 7.0f;
    float sf = d->maximum_shield_vitality > 0.0f ? p->shield / d->maximum_shield_vitality : 0.0f;
    float hf = p->body / d->maximum_body_vitality;
    g = hc_gfx_hud_rect(g, x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, HUD_BLUE_DIM);
    u32 sc = HUD_BLUE;
    if (sf < 0.25f) sc = (sinf(t * 18.0f) > 0.0f) ? HUD_RED : 0x802018FF;
    if (p->shield_flash > 0.0f) sc = hc_rgba_lerp(sc, 0xFFFFFFFF, p->shield_flash * 0.6f);
    g = hc_gfx_hud_rect(g, x, y, w * hc_clampf(sf, 0.0f, 1.0f), h, sc);
    /* Health: CE's segmented bar under the shield. */
    int segs = 8;
    float sw = (w - (segs - 1) * 1.5f) / segs;
    for (int i = 0; i < segs; i++) {
        float fill = hc_clampf(hf * segs - i, 0.0f, 1.0f);
        u32 c = hf < 0.3f ? HUD_RED : 0x7FD0FFFF;
        g = hc_gfx_hud_rect(g, x + i * (sw + 1.5f), y + h + 3.0f, sw, 3.0f, HUD_BLUE_DIM);
        if (fill > 0.0f) g = hc_gfx_hud_rect(g, x + i * (sw + 1.5f), y + h + 3.0f, sw * fill, 3.0f, c);
    }
    return g;
}

static Gfx* hud_ammo(Gfx* g, const HaloUnit* p, float t) {
    const HaloWeaponState* w = &p->weapon;
    if (w->id == HALO_WEAPON_NONE) return g;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    float x = 14.0f, y = 14.0f;
    int max = wd->rounds_loaded_maximum;
    int low = w->rounds_loaded <= (int)hc_maxf(1.0f, max / 6.0f);
    if (max > 60) {
        /* Flamethrower: a fuel gauge rather than 100 ticks. */
        float f = (float)w->rounds_loaded / (float)max;
        g = hc_gfx_hud_rect(g, x - 1.0f, y - 1.0f, 62.0f, 9.0f, HUD_BLUE_DIM);
        g = hc_gfx_hud_rect(g, x, y, 60.0f * f, 7.0f, low ? HUD_RED : HUD_BLUE);
    } else if (max > 0) {
        /* One tick per round: the AR's 3 x 20, chunkier ticks for small magazines. */
        int per_row = max < 20 ? max : 20;
        float tw = hc_clampf(60.0f / (float)per_row - 1.0f, 2.0f, 9.0f);
        float th = max <= 12 ? 8.0f : 5.0f;
        for (int i = 0; i < max; i++) {
            int row = i / per_row, col = i % per_row;
            u32 c = i < w->rounds_loaded ? (low ? HUD_RED : HUD_BLUE) : HUD_BLUE_DIM;
            g = hc_gfx_hud_rect(g, x + col * (tw + 1.0f), y + row * (th + 1.0f), tw, th, c);
        }
    } else {
        /* Covenant energy weapons: heat bar + charge. */
        g = hc_gfx_hud_rect(g, x - 1.0f, y - 1.0f, 62.0f, 7.0f, HUD_BLUE_DIM);
        u32 hc = w->overheated ? ((sinf(t * 20.0f) > 0.0f) ? HUD_RED : 0x801810FF) : hc_rgba_lerp(HUD_BLUE, HUD_RED, w->heat);
        g = hc_gfx_hud_rect(g, x, y, 60.0f * w->heat, 5.0f, hc);
        if (w->charge > 0.0f) {
            float cf = hc_clampf(w->charge / (wd->trigger.charging_time + 0.18f), 0.0f, 1.0f);
            g = hc_gfx_hud_rect(g, x, y + 8.0f, 60.0f * cf, 3.0f, cf >= 1.0f ? 0xB4FF9CFF : 0x6FA860FF);
        }
    }
    return g;
}

static Gfx* hud_grenades(Gfx* g, const HaloUnit* p) {
    /* Frags then plasmas; the type G will throw is outlined. */
    static const u32 k_gren[HALO_GRENADE_COUNT] = { [HALO_GRENADE_FRAG] = 0x8AA65AFF, [HALO_GRENADE_PLASMA] = 0x6CB6FFFF };
    static const HaloGrenadeId k_order[2] = { HALO_GRENADE_FRAG, HALO_GRENADE_PLASMA };
    float x = 14.0f, gy = 38.0f;
    for (int k = 0; k < 2; k++) {
        HaloGrenadeId type = k_order[k];
        float gx = x + k * 36.0f;
        if (p->grenade_type == type) g = hc_gfx_hud_rect(g, gx - 2.0f, gy - 2.0f, 34.0f, 10.0f, 0x4FB4FF60);
        for (int i = 0; i < g_halo_grenades[type].maximum_count; i++) {
            u32 c = i < p->grenades[type] ? k_gren[type] : 0x20304060;
            g = hc_gfx_hud_rect(g, gx + i * 8.0f, gy, 6.0f, 6.0f, c);
            if (i < p->grenades[type]) g = hc_gfx_hud_rect(g, gx + i * 8.0f + 2.0f, gy + 2.0f, 2.0f, 2.0f, 0xE8F4FFFF);
        }
    }
    return g;
}

static Gfx* hud_ring(Gfx* g, float cx, float cy, float r, int dots, u32 c) {
    for (int i = 0; i < dots; i++) {
        float a = (float)i * (2.0f * HC_PI / (float)dots);
        g = hc_gfx_hud_rect(g, cx + cosf(a) * r - 0.5f, cy + sinf(a) * r - 0.5f, 1.0f, 1.0f, c);
    }
    return g;
}

/* Black outside a circle (unless Halo's scope mask is drawn), thin crosshairs inside: the CE scope view. */
static Gfx* hud_scope(Gfx* g, int mask) {
    float cx = 160.0f, cy = 120.0f, r = 104.0f;
    for (float yy = 0.0f; mask && yy < 240.0f; yy += 2.0f) {
        float dy = yy + 1.0f - cy;
        float half = fabsf(dy) < r ? sqrtf(r * r - dy * dy) : 0.0f;
        g = hc_gfx_hud_rect(g, 0.0f, yy, cx - half, 2.0f, 0x000000FF);
        g = hc_gfx_hud_rect(g, cx + half, yy, 320.0f - (cx + half), 2.0f, 0x000000FF);
    }
    if (mask) g = hud_ring(g, cx, cy, r, 160, 0x4FB4FFA0);
    g = hc_gfx_hud_rect(g, cx - r, cy - 0.25f, r * 2.0f, 0.5f, 0x4FB4FF90);
    g = hc_gfx_hud_rect(g, cx - 0.25f, cy - r, 0.5f, r * 2.0f, 0x4FB4FF90);
    for (int i = 1; i <= 4; i++) {
        float o = (float)i * 12.0f;
        g = hc_gfx_hud_rect(g, cx - 3.0f, cy + o, 6.0f, 0.5f, 0x4FB4FFB0);
    }
    return g;
}

static int aiming_at_enemy(HaloSim* sim, const HaloUnit* p, const HcView* v) {
    const HaloWeaponDef* wd = p->weapon.id != HALO_WEAPON_NONE ? &g_halo_weapons[p->weapon.id] : NULL;
    if (!wd) return 0;
    hv3 eye = halo_unit_eye(p);
    hv3 dir = hv3_from_angles(v->yaw, v->pitch);
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        if (!u->active || u->dead || u->team == p->team) continue;
        hv3 c = hv3_make(u->pos.x, u->pos.y, u->pos.z + halo_unit_height(u) * 0.55f);
        hv3 to = hv3_sub(c, eye);
        float d = hv3_len(to);
        if (d > wd->magnetism_range * 1.5f || d < 0.01f) continue;
        float along = hv3_dot(to, dir);
        if (along <= 0.0f) continue;
        hv3 closest = hv3_mad(eye, dir, along);
        if (hv3_dist(closest, c) < halo_unit_def(u)->collision_radius * 1.6f) return 1;
    }
    return 0;
}

static Gfx* hud_reticle(Gfx* g, const HaloUnit* p, const HcView* v, int on_enemy) {
    float cx = 160.0f, cy = 120.0f;
    u32 c = on_enemy ? HUD_RED : HUD_BLUE;
    if (v->zoom > 1.01f) return hc_gfx_hud_rect(g, cx - 0.5f, cy - 0.5f, 1.0f, 1.0f, c);
    float err = p->weapon.error;
    switch (p->weapon.id) {
        case HALO_WEAPON_ASSAULT_RIFLE:
            g = hud_ring(g, cx, cy, 7.0f + err * 7.0f, 20, c);
            break;
        case HALO_WEAPON_PISTOL:
            g = hud_ring(g, cx, cy, 5.0f + err * 3.0f, 14, c);
            g = hc_gfx_hud_rect(g, cx - 9.0f, cy - 0.5f, 3.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx + 6.0f, cy - 0.5f, 3.0f, 1.0f, c);
            break;
        case HALO_WEAPON_SHOTGUN:
            g = hud_ring(g, cx, cy, 17.0f, 32, c);
            break;
        case HALO_WEAPON_SNIPER_RIFLE:
            g = hc_gfx_hud_rect(g, cx - 5.0f, cy - 0.5f, 3.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx + 2.0f, cy - 0.5f, 3.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy + 2.0f, 1.0f, 3.0f, c);
            break;
        case HALO_WEAPON_ROCKET_LAUNCHER:
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    g = hc_gfx_hud_rect(g, cx + sx * 11.0f - (sx > 0 ? 4.0f : 0.0f), cy + sy * 11.0f, 4.0f, 1.0f, c);
                    g = hc_gfx_hud_rect(g, cx + sx * 11.0f, cy + sy * 11.0f - (sy > 0 ? 4.0f : 0.0f), 1.0f, 4.0f, c);
                }
            }
            break;
        case HALO_WEAPON_FLAMETHROWER:
            g = hud_ring(g, cx, cy, 11.0f, 10, c);
            g = hud_ring(g, cx, cy, 5.0f, 6, c);
            break;
        case HALO_WEAPON_NEEDLER:
            for (int k = 0; k < 3; k++) {
                float a = -HC_PI * 0.5f + (float)k * (2.0f * HC_PI / 3.0f);
                for (int s = 0; s < 3; s++) {
                    float rr = 5.0f + err * 4.0f + (float)s * 1.5f;
                    g = hc_gfx_hud_rect(g, cx + cosf(a) * rr - 0.5f, cy + sinf(a) * rr - 0.5f, 1.0f, 1.0f, c);
                }
            }
            break;
        case HALO_WEAPON_FUEL_ROD:
            g = hud_ring(g, cx, cy, 9.0f, 18, c);
            g = hc_gfx_hud_rect(g, cx - 4.0f, cy - 0.5f, 8.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy - 4.0f, 1.0f, 8.0f, c);
            break;
        case HALO_WEAPON_PLASMA_RIFLE: {
            float o = 4.0f + err * 4.0f;
            g = hc_gfx_hud_rect(g, cx - o - 4.0f, cy - 0.5f, 4.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx + o, cy - 0.5f, 4.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy - o - 4.0f, 1.0f, 4.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy + o, 1.0f, 4.0f, c);
            break;
        }
        default:
            /* Plasma pistol: four arrows around a gap. */
            g = hc_gfx_hud_rect(g, cx - 6.0f, cy - 0.5f, 3.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx + 3.0f, cy - 0.5f, 3.0f, 1.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy - 6.0f, 1.0f, 3.0f, c);
            g = hc_gfx_hud_rect(g, cx - 0.5f, cy + 3.0f, 1.0f, 3.0f, c);
            return g;
    }
    return hc_gfx_hud_rect(g, cx - 0.5f, cy - 0.5f, 1.0f, 1.0f, c);
}

static Gfx* hud_damage_dir(Gfx* g, const HaloUnit* p, const HcView* v) {
    if (p->last_damage_age > 1.0f || p->last_attacker < 0) return g;
    float a = 1.0f - p->last_damage_age;
    hv3 d = p->last_damage_dir;
    float rel = hc_wrap_angle(atan2f(d.y, d.x) - v->yaw); /* 0 = ahead, + = left */
    float sx = 160.0f - sinf(rel) * 70.0f;
    float sy = 120.0f - cosf(rel) * 55.0f;
    u32 c = (HUD_RED & 0xFFFFFF00) | (u32)(a * 200.0f);
    return hc_gfx_hud_rect(g, sx - 6.0f, sy - 6.0f, 12.0f, 12.0f, c);
}

static Gfx* hud_tracker(Gfx* g, HaloSim* sim, const HaloUnit* p, const HcView* v, float t) {
    float cx = 42.0f, cy = 200.0f, r = 26.0f;
    for (int i = -4; i <= 4; i++) {
        float yy = (float)i / 4.5f;
        float half = r * sqrtf(hc_maxf(0.0f, 1.0f - yy * yy));
        g = hc_gfx_hud_rect(g, cx - half, cy + yy * r - r / 9.0f, half * 2.0f, r / 4.5f, 0x0A1E3C70);
    }
    g = hc_gfx_hud_rect(g, cx - r, cy, r * 2.0f, 0.5f, 0x4FB4FF50);
    g = hc_gfx_hud_rect(g, cx, cy - r, 0.5f, r * 2.0f, 0x4FB4FF50);
    g = hc_gfx_hud_rect(g, cx - 1.5f, cy - 1.5f, 3.0f, 3.0f, 0xFFE65AFF);
    for (int i = 0; i < HALO_MAX_UNITS; i++) {
        const HaloUnit* u = &sim->units[i];
        if (!u->active || u->dead || u->is_player) continue;
        /* CE only shows units that move or shoot. */
        if (hv3_len_xy(u->vel) < 0.15f && u->weapon.fire_flash <= 0.0f) continue;
        hv3 d = hv3_sub(u->pos, p->pos);
        float dist = hv3_len_xy(d);
        if (dist > HC_TRACKER_RANGE) continue;
        float rel = hc_wrap_angle(atan2f(d.y, d.x) - v->yaw);
        float k = dist / HC_TRACKER_RANGE * r;
        float px = cx - sinf(rel) * k, py = cy - cosf(rel) * k;
        float pulse = 0.6f + 0.4f * sinf(t * 8.0f);
        u32 c = u->team == p->team ? 0xFFE65AFF : (0xFF3A2A00 | (u32)(pulse * 255.0f));
        if (u->team == HALO_TEAM_NEUTRAL) c = 0xE8E8E8C0;
        g = hc_gfx_hud_rect(g, px - 2.0f, py - 2.0f, 4.0f, 4.0f, c);
    }
    return g;
}

void hc_draw_hud(GAME_PLAY* play, HaloSim* sim, const HcView* v, const HcHudText* text) {
    GRAPH* graph = play->game.graph;
    GAME* game = &play->game;
    HcGfxRedirect r;
    hc_gfx_begin(graph, &r, HC_GFX_FONT | HC_GFX_OPA);
    OPEN_DISP(graph);

    HaloUnit* p = halo_player(sim);
    Gfx* g = NOW_FONT_DISP;
    g = hc_gfx_hud_mode(g);
    unsigned drawn = 0;
    if (p && v->first_person) {
        if (!p->dead) {
            int on_enemy = aiming_at_enemy(sim, p, v);
            g = hc_hud_view_draw(g, sim, p, v, on_enemy, &drawn);
            if (v->zoom > 1.01f) g = hud_scope(g, !(drawn & HC_HUD_DREW_SCOPE));
            if (!(drawn & HC_HUD_DREW_UNIT)) g = hud_shield(g, p, v->time);
            if (!(drawn & HC_HUD_DREW_WEAPON)) g = hud_ammo(g, p, v->time);
            if (!(drawn & HC_HUD_DREW_GRENADES)) g = hud_grenades(g, p);
            if (!(drawn & HC_HUD_DREW_RETICLE)) g = hud_reticle(g, p, v, on_enemy);
            if (!(drawn & HC_HUD_DREW_DAMAGE)) g = hud_damage_dir(g, p, v);
            if (!(drawn & HC_HUD_DREW_SENSOR)) g = hud_tracker(g, sim, p, v, v->time);
            if (p->hurt_flash > 0.0f) {
                g = hc_gfx_hud_rect(g, 0.0f, 0.0f, 320.0f, 240.0f, 0xC0100000 | (u32)(p->hurt_flash * 70.0f));
            }
        } else {
            g = hc_gfx_hud_rect(g, 0.0f, 0.0f, 320.0f, 240.0f, 0x30000060);
        }
    }
    SET_FONT_DISP(g);

    /* pc_text_draw's glyphs use the current matrices; only host UI that is
     * open this frame loads the font ortho, so load it ourselves. */
    mFont_SetMatrix(graph, 1);

    char buf[64];
    if (p && v->first_person && !p->dead) {
        const HaloWeaponState* w = &p->weapon;
        if (w->id != HALO_WEAPON_NONE) {
            const HaloWeaponDef* wd = &g_halo_weapons[w->id];
            int halo_panel = drawn & HC_HUD_DREW_WEAPON, halo_warnings = drawn & HC_HUD_DREW_WARNINGS;
            if (wd->rounds_loaded_maximum > 0) {
                snprintf(buf, sizeof(buf), "%d", w->rounds_reserve);
                if (!halo_panel) pc_text_draw(game, buf, 76.0f, 12.0f, 0x4F, 0xB4, 0xFF, 255, 0.55f);
                if (!halo_warnings && w->reload_timer > 0.0f)
                    pc_text_draw(game, "RELOADING", 136.0f, 150.0f, 0x4F, 0xB4, 0xFF, 220, 0.45f);
                else if (!halo_warnings && w->rounds_loaded == 0 && w->rounds_reserve == 0)
                    pc_text_draw(game, "NO AMMO", 140.0f, 150.0f, 0xFF, 0x3A, 0x2A, 230, 0.45f);
            } else if (!halo_panel) {
                snprintf(buf, sizeof(buf), "%d%%", (int)(w->battery * 100.0f + 0.5f));
                pc_text_draw(game, buf, 80.0f, 10.0f, 0x4F, 0xB4, 0xFF, 255, 0.5f);
                if (w->overheated) pc_text_draw(game, "OVERHEATED", 132.0f, 150.0f, 0xFF, 0x3A, 0x2A, 230, 0.45f);
            }
            if (!halo_panel) pc_text_draw(game, wd->hud_name, 14.0f, 48.0f, 0x4F, 0xB4, 0xFF, 200, 0.38f);
            if (v->zoom > 1.01f) {
                snprintf(buf, sizeof(buf), "%.0fx", v->zoom);
                pc_text_draw(game, buf, 152.0f, 196.0f, 0x4F, 0xB4, 0xFF, 230, 0.5f);
            }
        }
        if (sim->arsenal_enabled && v->weapon_switch_age < 1.6f) {
            float fade = hc_clampf((1.6f - v->weapon_switch_age) / 0.4f, 0.0f, 1.0f);
            for (int k = 0; k < HALO_WEAPON_COUNT; k++) {
                if (!sim->arsenal_owned[k]) continue;
                int cur = k == w->id;
                snprintf(buf, sizeof(buf), "%d %s", (k + 1) % 10, g_halo_weapons[k].hud_name);
                float tw = (float)pc_text_width(buf) * 0.36f;
                int a = (int)((cur ? 255.0f : 130.0f) * fade);
                if (cur) pc_text_draw(game, buf, 310.0f - tw, 58.0f + k * 8.0f, 0xFF, 0xFF, 0xFF, a, 0.36f);
                else pc_text_draw(game, buf, 310.0f - tw, 58.0f + k * 8.0f, 0x4F, 0xB4, 0xFF, a, 0.36f);
            }
        }
    }
    if (text) {
        for (int i = 0; i < text->label_count; i++) {
            const HcWorldLabel* l = &text->labels[i];
            float sx, sy;
            if (l->alpha <= 0.0f || !project(play, l->pos, &sx, &sy)) continue;
            float tw = (float)pc_text_width(l->text) * 0.4f;
            int a = (int)(hc_clampf(l->alpha, 0.0f, 1.0f) * 255.0f);
            pc_text_draw(game, l->text, sx - tw * 0.5f + 0.6f, sy - 7.4f, 0x10, 0x10, 0x10, a / 2, 0.4f);
            pc_text_draw(game, l->text, sx - tw * 0.5f, sy - 8.0f, HC_R(l->rgb), HC_G(l->rgb), HC_B(l->rgb), a, 0.4f);
        }
    }
    if (p && v->first_person && p->dead) {
        snprintf(buf, sizeof(buf), "RESPAWN IN %d", (int)(sim->player_respawn_timer + 0.99f));
        pc_text_draw(game, buf, 118.0f, 112.0f, 0xFF, 0xFF, 0xFF, 230, 0.6f);
    }
    if (text && text->center[0] && text->center_alpha > 0.0f) {
        float w = (float)pc_text_width(text->center) * 0.6f;
        pc_text_draw(game, text->center, 160.0f - w * 0.5f, 60.0f, 0xFF, 0xF0, 0xC0, (int)(text->center_alpha * 255.0f),
                     0.6f);
    }

    if (v->show_ai) {
        for (int i = 0; i < HALO_MAX_UNITS; i++) {
            const HaloUnit* u = &sim->units[i];
            if (!u->active || u->is_player || !sim->ai[i].active) continue;
            if (p && hv3_dist(u->pos, p->pos) > 22.0f) continue;
            hv3 head = halo_unit_pos_interp(u, sim->alpha);
            head.z += halo_unit_height(u) + 0.12f;
            float sx, sy;
            if (!project(play, hc_h2a_pos(head), &sx, &sy)) continue;
            const HaloAi* a = &sim->ai[i];
            snprintf(buf, sizeof(buf), "%s %s %.0f/%.0f", g_halo_actors[a->type].name, halo_ai_state_name(a->state),
                     u->shield, u->body);
            float w = (float)pc_text_width(buf) * 0.32f;
            pc_text_draw(game, buf, sx - w * 0.5f, sy - 8.0f, 0xFF, 0xFF, 0x80, 230, 0.32f);
        }
    }

    if (text) {
        for (int i = 0; i < text->count; i++) {
            pc_text_draw(game, text->lines[i], 4.0f, 56.0f + i * 7.0f, 0xE8, 0xE8, 0xE8, 220, 0.33f);
        }
    }

    mFont_UnSetMatrix(graph, 1);
    CLOSE_DISP(graph);
    hc_gfx_end(graph, &r);
}
