#include "hc_hud_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hc_hud_pack.h"

#ifndef HC_HALO_ASSET_DIR
#define HC_HALO_ASSET_DIR "assets_local/halo/generated"
#endif

#define HUD_W 640.0f
#define HUD_H 480.0f
#define TO_SCREEN 0.5f        /* CE's 640x480 HUD onto the 320x240 frame */
#define BLIP_SIZE 14.0f       /* HUD pixels */
#define SHIELD_LOW 0.25f
#define HEALTH_HIGH 0.67f     /* CE's medium and low health colour cutoffs */
#define HEALTH_LOW 0.33f
#define OTHER_GRENADE_DX 60.0f /* the type G does not throw, beside the current one */
#define OTHER_GRENADE_FADE 0.5f
#define ENEMY_RED 0xFF3A2AFF

static struct {
    HcHudPack pack;
    const HcHudElement* digits[10];
    int tried;
    char status[160];
} s;

void hc_hud_view_init(void) {
    if (s.tried) return;
    s.tried = 1;
    const char* dir = getenv("HC_HALO_ASSETS");
    char path[512], err[128];
    snprintf(path, sizeof(path), "%s/%s", dir && dir[0] ? dir : HC_HALO_ASSET_DIR, HC_HUD_PACK_NAME);
    if (hc_hud_pack_load(&s.pack, path, err, sizeof(err))) {
        for (int d = 0; d < 10; d++) s.digits[d] = hc_hud_find(&s.pack, HC_HUD_DIGIT, HC_HUD_GROUP_GLOBAL, d);
        snprintf(s.status, sizeof(s.status), "Halo HUD: %d elements, %d textures", s.pack.element_count,
                 s.pack.texture_count);
    } else {
        snprintf(s.status, sizeof(s.status), "Halo HUD: placeholder (%s)", err);
    }
    printf("[halo-crossing] %s [%s]\n", s.status, path);
    fflush(stdout);
}

const char* hc_hud_view_status(void) {
    return s.tried ? s.status : "Halo HUD: not loaded";
}

static float corner_x(int anchor) {
    if (anchor == HC_HUD_CENTRE) return HUD_W * 0.5f;
    return (anchor == HC_HUD_TOP_RIGHT || anchor == HC_HUD_BOTTOM_RIGHT) ? HUD_W : 0.0f;
}

static float corner_y(int anchor) {
    if (anchor == HC_HUD_CENTRE) return HUD_H * 0.5f;
    return (anchor == HC_HUD_BOTTOM_LEFT || anchor == HC_HUD_BOTTOM_RIGHT) ? HUD_H : 0.0f;
}

static u32 faded(u32 c, float k) {
    return (c & 0xFFFFFF00) | (u32)(hc_clampf(k, 0.0f, 1.0f) * (float)HC_A(c));
}

/* Texels (sx, sy, sw, sh) of `tex` stretched over the HUD-space rect (x, y, w, h). */
static Gfx* stretch(Gfx* g, int tex, float x, float y, float w, float h, float sx, float sy, float sw, float sh,
                    u32 rgba) {
    if (w <= 0.0f || h <= 0.0f) return g;
    const HcHudTexture* t = &s.pack.textures[tex];
    return hc_gfx_hud_sprite(g, hc_hud_texture_pixels(&s.pack, tex), t->width, t->height, x * TO_SCREEN,
                             y * TO_SCREEN, w * TO_SCREEN, h * TO_SCREEN, sx, sy, sw / (w * TO_SCREEN),
                             sh / (h * TO_SCREEN), rgba);
}

static Gfx* blit(Gfx* g, int tex, float x, float y, float sx, float sy, float w, float h, u32 rgba) {
    return stretch(g, tex, x, y, w, h, sx, sy, w, h, rgba);
}

static Gfx* element(Gfx* g, const HcHudElement* e, float dx, float dy, u32 rgba) {
    if (e == NULL || e->texture == HC_HUD_NO_TEXTURE) return g;
    return blit(g, e->texture, corner_x(e->anchor) + e->x + dx, corner_y(e->anchor) + e->y + dy, 0.0f, 0.0f, e->w,
                e->h, rgba);
}

static int has_group(int kind, int group) {
    return hc_hud_find(&s.pack, kind, group, -1) != NULL;
}

/* Every static of `group` in `state` (< 0: any), in its default or flash colour. */
static Gfx* statics(Gfx* g, int group, int state, float dx, float k, int flash) {
    for (int i = 0; i < s.pack.element_count; i++) {
        const HcHudElement* e = &s.pack.elements[i];
        if (e->kind != HC_HUD_STATIC || e->group != group || (state >= 0 && e->state != state)) continue;
        g = element(g, e, dx, 0.0f, faded(e->colour[flash ? 1 : 0], k));
    }
    return g;
}

/* Lit cells or columns in `lit`, the rest in `empty` (alpha 0: not drawn). */
static Gfx* meter(Gfx* g, const HcHudElement* e, float value, u32 lit, u32 empty) {
    float x0 = corner_x(e->anchor) + e->x, y0 = corner_y(e->anchor) + e->y;
    const HcHudCell* cells = &s.pack.cells[e->first_cell];
    if (e->flags & HC_HUD_METER_CONTINUOUS) {
        const HcHudCell* c = &cells[0];
        const uint8_t* level = hc_hud_cell_columns(&s.pack, c);
        float v = hc_hud_meter_value(e, value);
        for (int a = 0; a < c->columns;) {
            if (level[a] == HC_HUD_COLUMN_EMPTY) {
                a++;
                continue;
            }
            int on = hc_hud_column_lit(level[a], v), b = a + 1;
            while (b < c->columns && level[b] != HC_HUD_COLUMN_EMPTY && hc_hud_column_lit(level[b], v) == on) b++;
            u32 col = on ? lit : empty;
            if (HC_A(col)) g = blit(g, c->texture, x0 + c->x + a, y0 + c->y, (float)a, 0.0f, (float)(b - a), e->h, col);
            a = b;
        }
        return g;
    }
    int n = hc_hud_ticks_lit(e, value);
    for (int i = 0; i < e->cell_count; i++) {
        const HcHudCell* c = &cells[i];
        const HcHudTexture* t = &s.pack.textures[c->texture];
        u32 col = i < n ? lit : empty;
        if (HC_A(col)) g = blit(g, c->texture, x0 + c->x, y0 + c->y, 0.0f, 0.0f, t->width, t->height, col);
    }
    return g;
}

/* CE's counter digits; flags carry the digit count and, in bit 8, leading zeros. */
static Gfx* number(Gfx* g, const HcHudElement* e, int value, float dx, u32 rgba) {
    int digits = e->flags & 0xFF, zeros = (e->flags >> 8) & 1;
    if (digits < 1 || digits > 6) digits = 3;
    int top = 1;
    for (int i = 0; i < digits; i++) top *= 10;
    value = value < 0 ? 0 : value >= top ? top - 1 : value;
    char buf[8];
    snprintf(buf, sizeof(buf), zeros ? "%0*d" : "%*d", digits, value);
    float x = corner_x(e->anchor) + e->x + dx, y = corner_y(e->anchor) + e->y;
    for (const char* c = buf; *c; c++) {
        const HcHudElement* d = s.digits[*c >= '0' && *c <= '9' ? *c - '0' : 0];
        if (d == NULL) return g;
        if (*c != ' ') g = blit(g, d->texture, x + d->x, y + d->y, 0.0f, 0.0f, d->w, d->h, rgba);
        x += (float)(d->flags & 0xFF);
    }
    return g;
}

static Gfx* draw_unit(Gfx* g, const HaloUnit* p, float wave, unsigned* drawn) {
    const HcHudElement* sm = hc_hud_find(&s.pack, HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD);
    const HcHudElement* hm = hc_hud_find(&s.pack, HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_HEALTH);
    if (sm == NULL || hm == NULL) return g;
    const HaloBipedDef* d = halo_unit_def(p);
    float sf = d->maximum_shield_vitality > 0.0f ? p->shield / d->maximum_shield_vitality : 0.0f;
    float hf = p->body / d->maximum_body_vitality;
    int low = sf < SHIELD_LOW;
    g = statics(g, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD, 0.0f, 1.0f, low && wave > 0.5f);
    u32 sc = hc_rgba_lerp(sm->colour[HC_HUD_C_MIN], sm->colour[HC_HUD_C_MAX], hc_clampf(sf, 0.0f, 1.0f));
    if (p->shield_flash > 0.0f) sc = hc_rgba_lerp(sc, sm->colour[HC_HUD_C_FLASH], p->shield_flash * 0.6f);
    g = meter(g, sm, sf, sc, sm->colour[HC_HUD_C_EMPTY]);
    g = statics(g, HC_HUD_GROUP_UNIT, HC_HUD_HEALTH, 0.0f, 1.0f, 0);
    u32 hc = hf > HEALTH_HIGH ? hm->colour[HC_HUD_C_MAX] : hf > HEALTH_LOW ? hm->colour[HC_HUD_C_EXTRA]
                                                                          : hm->colour[HC_HUD_C_MIN];
    if (hf <= HEALTH_LOW) hc = hc_rgba_lerp(hc, hm->colour[HC_HUD_C_FLASH], wave);
    g = meter(g, hm, hf, hc, hm->colour[HC_HUD_C_EMPTY]);
    *drawn |= HC_HUD_DREW_UNIT;
    return g;
}

static Gfx* draw_weapon(Gfx* g, const HaloUnit* p, const HcView* v, int on_enemy, float wave, int blink,
                        unsigned* drawn) {
    const HaloWeaponState* w = &p->weapon;
    if (w->id == HALO_WEAPON_NONE) return g;
    const HaloWeaponDef* wd = &g_halo_weapons[w->id];
    int group = HC_HUD_GROUP_WEAPON(w->id);
    const HcHudElement* cut = hc_hud_find(&s.pack, HC_HUD_CUTOFFS, group, -1);
    int total = w->rounds_loaded + w->rounds_reserve;
    float heat_cut = cut && cut->w > 0 ? cut->w / 100.0f : 1.0f;
    float value[4] = {
        [HC_HUD_TOTAL_AMMO] = (float)total / (float)(wd->rounds_total_maximum > 0 ? wd->rounds_total_maximum : 1),
        [HC_HUD_LOADED_AMMO] = (float)w->rounds_loaded / (float)(wd->rounds_loaded_maximum > 0 ? wd->rounds_loaded_maximum : 1),
        [HC_HUD_HEAT] = w->heat,
        [HC_HUD_AGE] = w->battery,
    };
    int shown[4] = { w->rounds_reserve, w->rounds_loaded, (int)(w->heat * 100.0f + 0.5f),
                     (int)(w->battery * 100.0f + 0.5f) };

    if (has_group(HC_HUD_METER, group) || has_group(HC_HUD_NUMBER, group)) {
        g = statics(g, group, -1, 0.0f, 1.0f, 0);
        for (int i = 0; i < s.pack.element_count; i++) {
            const HcHudElement* e = &s.pack.elements[i];
            if (e->group != group || e->state > HC_HUD_AGE) continue;
            float val = value[e->state];
            if (e->kind == HC_HUD_METER) {
                u32 lit = hc_rgba_lerp(e->colour[HC_HUD_C_MIN], e->colour[HC_HUD_C_MAX], hc_clampf(val, 0.0f, 1.0f));
                if (e->state == HC_HUD_HEAT && (w->overheated || w->heat >= heat_cut))
                    lit = hc_rgba_lerp(lit, e->colour[HC_HUD_C_FLASH], wave);
                g = meter(g, e, val, lit, e->colour[HC_HUD_C_EMPTY]);
            } else if (e->kind == HC_HUD_NUMBER) {
                g = number(g, e, shown[e->state], 0.0f, e->colour[0]);
            }
        }
        *drawn |= HC_HUD_DREW_WEAPON;
    }

    if (has_group(HC_HUD_WARNING, group)) {
        int warn = 0;
        if (wd->rounds_loaded_maximum > 0) {
            if (total == 0) warn = HC_HUD_WARN_NO_AMMO;
            else if (w->reload_timer <= 0.0f && w->rounds_reserve > 0 && cut && w->rounds_loaded < cut->y)
                warn = HC_HUD_WARN_RELOAD;
            else if (cut && total < cut->x) warn = HC_HUD_WARN_LOW_AMMO;
        } else if (w->battery <= 0.0f) {
            warn = HC_HUD_WARN_DEPLETED;
        } else if (cut && w->battery * 100.0f < cut->h) {
            warn = HC_HUD_WARN_LOW_BATTERY;
        }
        const HcHudElement* e = warn ? hc_hud_find(&s.pack, HC_HUD_WARNING, group, warn) : NULL;
        if (e && blink) g = element(g, e, 0.0f, 0.0f, e->colour[0]);
        *drawn |= HC_HUD_DREW_WARNINGS;
    }

    const HcHudElement* reticle = hc_hud_find(&s.pack, HC_HUD_RETICLE, group, -1);
    if (reticle && v->zoom <= 1.01f) {
        g = element(g, reticle, 0.0f, 0.0f, on_enemy ? ENEMY_RED : reticle->colour[0]);
        *drawn |= HC_HUD_DREW_RETICLE;
    }
    return g;
}

static Gfx* draw_grenade_panel(Gfx* g, int group, int count, float dx, float k) {
    g = statics(g, group, HC_HUD_GREN_BG, dx, k, 0);
    g = statics(g, group, HC_HUD_GREN_COUNT_BG, dx, k, 0);
    const HcHudElement* icon = hc_hud_find(&s.pack, HC_HUD_STATIC, group, count > 0 ? HC_HUD_GREN_ICON : HC_HUD_GREN_ICON_EMPTY);
    if (icon == NULL) icon = hc_hud_find(&s.pack, HC_HUD_STATIC, group, HC_HUD_GREN_ICON);
    if (icon) g = element(g, icon, dx, 0.0f, faded(icon->colour[0], k));
    const HcHudElement* num = hc_hud_find(&s.pack, HC_HUD_NUMBER, group, HC_HUD_GREN_NUMBER);
    if (num) g = number(g, num, count, dx, faded(num->colour[0], k));
    return g;
}

static Gfx* draw_grenades(Gfx* g, const HaloUnit* p, unsigned* drawn) {
    /* The pack keeps CE's order: frags, then plasmas. */
    static const int k_panel[HALO_GRENADE_COUNT] = { [HALO_GRENADE_FRAG] = 0, [HALO_GRENADE_PLASMA] = 1 };
    HaloGrenadeId cur = p->grenade_type;
    HaloGrenadeId other = cur == HALO_GRENADE_FRAG ? HALO_GRENADE_PLASMA : HALO_GRENADE_FRAG;
    int gc = HC_HUD_GROUP_GRENADE(k_panel[cur]), go = HC_HUD_GROUP_GRENADE(k_panel[other]);
    if (!has_group(HC_HUD_STATIC, gc) || !has_group(HC_HUD_STATIC, go)) return g;
    g = draw_grenade_panel(g, gc, p->grenades[cur], 0.0f, 1.0f);
    g = draw_grenade_panel(g, go, p->grenades[other], OTHER_GRENADE_DX, OTHER_GRENADE_FADE);
    *drawn |= HC_HUD_DREW_GRENADES;
    return g;
}

static Gfx* draw_sensor(Gfx* g, HaloSim* sim, const HaloUnit* p, const HcView* v, unsigned* drawn) {
    const HcHudElement* se = hc_hud_find(&s.pack, HC_HUD_SENSOR, HC_HUD_GROUP_UNIT, -1);
    const HcHudElement* blip = hc_hud_find(&s.pack, HC_HUD_BLIP, HC_HUD_GROUP_GLOBAL, -1);
    if (se == NULL || blip == NULL || se->w <= 0) return g;
    float cx = corner_x(se->anchor) + se->x, cy = corner_y(se->anchor) + se->y, r = se->w;
    g = statics(g, HC_HUD_GROUP_UNIT, HC_HUD_SENSOR_BG, 0.0f, 1.0f, 0);
    g = statics(g, HC_HUD_GROUP_UNIT, HC_HUD_SENSOR_FG, 0.0f, 1.0f, 0);
    float pulse = 0.8f + 0.2f * sinf(v->time * 8.0f);
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
        u32 c = u->team == p->team ? 0xFFE65AFF : faded(ENEMY_RED, pulse);
        if (u->team == HALO_TEAM_NEUTRAL) c = 0xE8E8E8C0;
        g = stretch(g, blip->texture, px - BLIP_SIZE * 0.5f, py - BLIP_SIZE * 0.5f, BLIP_SIZE, BLIP_SIZE, 0.0f, 0.0f,
                    blip->w, blip->h, c);
    }
    *drawn |= HC_HUD_DREW_SENSOR;
    return g;
}

/* CE's red arcs at the screen edge facing the attacker: ahead, right, behind, left. */
static Gfx* draw_damage(Gfx* g, const HaloUnit* p, const HcView* v, unsigned* drawn) {
    const HcHudElement* arc[4];
    for (int k = 0; k < 4; k++) {
        arc[k] = hc_hud_find(&s.pack, HC_HUD_DAMAGE, HC_HUD_GROUP_GLOBAL, k);
        if (arc[k] == NULL) return g;
    }
    *drawn |= HC_HUD_DREW_DAMAGE;
    if (p->last_damage_age > 1.0f || p->last_attacker < 0) return g;
    float fade = 1.0f - p->last_damage_age;
    hv3 d = p->last_damage_dir;
    float rel = hc_wrap_angle(atan2f(d.y, d.x) - v->yaw); /* 0 = ahead, + = left */
    float weight[4] = { cosf(rel), -sinf(rel), -cosf(rel), sinf(rel) };
    for (int k = 0; k < 4; k++) {
        const HcHudElement* e = arc[k];
        if (weight[k] <= 0.0f) continue;
        float m = e->x, x, y;
        switch (k) {
            case 0: x = (HUD_W - e->w) * 0.5f, y = m; break;
            case 1: x = HUD_W - m - e->w, y = (HUD_H - e->h) * 0.5f; break;
            case 2: x = (HUD_W - e->w) * 0.5f, y = HUD_H - m - e->h; break;
            default: x = m, y = (HUD_H - e->h) * 0.5f; break;
        }
        g = blit(g, e->texture, x, y, 0.0f, 0.0f, e->w, e->h, faded(e->colour[0], weight[k] * fade));
    }
    return g;
}

static Gfx* draw_scope(Gfx* g, const HaloUnit* p, const HcView* v, unsigned* drawn) {
    int which = p->weapon.id == HALO_WEAPON_SNIPER_RIFLE ? 0 : p->weapon.id == HALO_WEAPON_PISTOL ? 1 : -1;
    const HcHudElement* e = which >= 0 ? hc_hud_find(&s.pack, HC_HUD_SCOPE, HC_HUD_GROUP_GLOBAL, which) : NULL;
    if (e == NULL || v->zoom <= 1.01f) return g;
    const HcHudTexture* t = &s.pack.textures[e->texture];
    g = stretch(g, e->texture, 0.0f, 0.0f, HUD_W, HUD_H, 0.0f, 0.0f, t->width, t->height, e->colour[0]);
    *drawn |= HC_HUD_DREW_SCOPE;
    return g;
}

Gfx* hc_hud_view_draw(Gfx* g, HaloSim* sim, const HaloUnit* p, const HcView* v, int on_enemy, unsigned* drawn) {
    *drawn = 0;
    if (!s.pack.loaded || p == NULL) return g;
    float wave = 0.5f + 0.5f * sinf(v->time * 14.0f);
    int blink = fmodf(v->time, 0.8f) < 0.5f;
    g = hc_gfx_hud_sprite_mode(g);
    g = draw_scope(g, p, v, drawn);
    g = draw_damage(g, p, v, drawn);
    g = draw_unit(g, p, wave, drawn);
    g = draw_weapon(g, p, v, on_enemy, wave, blink, drawn);
    g = draw_grenades(g, p, drawn);
    g = draw_sensor(g, sim, p, v, drawn);
    return hc_gfx_hud_mode(g);
}
