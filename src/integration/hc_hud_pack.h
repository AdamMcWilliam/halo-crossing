/* Halo CE's HUD from the user's own Xbox maps: assets_local/halo/generated/
 * hud.hcpk, written by tools/halo_import/hud_pack.py (format documented
 * there). Placed sprites, meters pre-split into fill cells, counter digits,
 * reticles and warnings, all in CE's 640x480 HUD space. */
#ifndef HC_HUD_PACK_H
#define HC_HUD_PACK_H

#include <stddef.h>
#include <stdint.h>

#define HC_HUD_PACK_NAME "hud.hcpk"
#define HC_HUD_NO_TEXTURE 0xFFFF
#define HC_HUD_METER_CONTINUOUS 0x100 /* flags; the low byte is the meter's minimum lit value */
#define HC_HUD_COLUMN_EMPTY 255

enum HcHudKind {
    HC_HUD_STATIC,
    HC_HUD_METER,
    HC_HUD_NUMBER,
    HC_HUD_RETICLE,
    HC_HUD_WARNING,
    HC_HUD_DIGIT,
    HC_HUD_BLIP,
    HC_HUD_SCOPE,
    HC_HUD_DAMAGE,
    HC_HUD_SENSOR,
    HC_HUD_CUTOFFS, /* x, y, w, h: CE's total / loaded / heat / age warning cutoffs */
    HC_HUD_KIND_COUNT
};

/* Groups: the player unit, weapon k (HaloWeaponId order), grenade type t, shared pieces. */
#define HC_HUD_GROUP_UNIT 0
#define HC_HUD_GROUP_WEAPON(k) (1 + (k))
#define HC_HUD_GROUP_GRENADE(t) (11 + (t))
#define HC_HUD_GROUP_GLOBAL 13
#define HC_HUD_GROUP_COUNT 14

enum { HC_HUD_SHIELD, HC_HUD_HEALTH, HC_HUD_SENSOR_BG, HC_HUD_SENSOR_FG };             /* unit states */
enum { HC_HUD_TOTAL_AMMO, HC_HUD_LOADED_AMMO, HC_HUD_HEAT, HC_HUD_AGE };                /* weapon states */
enum { HC_HUD_GREN_BG, HC_HUD_GREN_COUNT_BG, HC_HUD_GREN_NUMBER, HC_HUD_GREN_ICON, HC_HUD_GREN_ICON_EMPTY };
/* Warning states are CE's crosshair types. */
enum {
    HC_HUD_WARN_RELOAD = 3,
    HC_HUD_WARN_LOW_BATTERY = 6,
    HC_HUD_WARN_NO_AMMO = 8,
    HC_HUD_WARN_NO_GRENADES = 9,
    HC_HUD_WARN_LOW_AMMO = 10,
    HC_HUD_WARN_DEPLETED = 18,
};
enum { HC_HUD_TOP_LEFT, HC_HUD_TOP_RIGHT, HC_HUD_BOTTOM_LEFT, HC_HUD_BOTTOM_RIGHT, HC_HUD_CENTRE };
/* Meter colour slots. */
enum { HC_HUD_C_MIN, HC_HUD_C_MAX, HC_HUD_C_EMPTY, HC_HUD_C_FLASH, HC_HUD_C_EXTRA };

typedef struct HcHudTexture {
    uint16_t width, height; /* GameCube RGBA8, multiples of 4 */
    uint32_t offset;
} HcHudTexture;

typedef struct HcHudElement {
    uint8_t kind, group, state, anchor;
    int16_t x, y, w, h; /* top-left relative to the anchor corner */
    uint16_t texture, first_cell, cell_count, flags;
    uint32_t colour[5]; /* 0xRRGGBBAA */
} HcHudElement;

typedef struct HcHudCell {
    uint16_t texture;
    int16_t x, y;            /* relative to the meter */
    uint16_t columns;        /* 0: a tick; else per-column fill levels for a continuous bar */
    uint32_t columns_offset;
    uint32_t pad;
} HcHudCell;

typedef struct HcHudPack {
    void* allocation;
    const uint8_t* data; /* 32-byte aligned */
    size_t size;
    int loaded;
    int texture_count, element_count, cell_count;
    const HcHudTexture* textures;
    const HcHudElement* elements;
    const HcHudCell* cells;
} HcHudPack;

int hc_hud_pack_load(HcHudPack* p, const char* path, char* err, size_t n);
void hc_hud_pack_free(HcHudPack* p);

const void* hc_hud_texture_pixels(const HcHudPack* p, int texture);
const uint8_t* hc_hud_cell_columns(const HcHudPack* p, const HcHudCell* c);
/* First element of `kind` in `group` with `state` (state < 0: any), or NULL. */
const HcHudElement* hc_hud_find(const HcHudPack* p, int kind, int group, int state);

/* A meter's value raised to its minimum lit value (when not empty), 0..1. */
float hc_hud_meter_value(const HcHudElement* e, float value);
/* Tick meters: how many cells light at `value`. */
int hc_hud_ticks_lit(const HcHudElement* e, float value);
/* Continuous meters: whether a column at fill `level` lights at `value`. */
int hc_hud_column_lit(uint8_t level, float value);

#endif
