#include "hc_hud_pack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_VERSION 1

typedef struct PackHeader {
    char magic[4];
    uint32_t version;
    uint32_t texture_count, textures_offset;
    uint32_t element_count, elements_offset;
    uint32_t cell_count, cells_offset;
} PackHeader;

typedef char check_header[sizeof(PackHeader) == 32 ? 1 : -1];
typedef char check_texture[sizeof(HcHudTexture) == 8 ? 1 : -1];
typedef char check_element[sizeof(HcHudElement) == 40 ? 1 : -1];
typedef char check_cell[sizeof(HcHudCell) == 16 ? 1 : -1];

static int fail(char* err, size_t n, const char* msg, int index) {
    if (err && n) {
        if (index >= 0) snprintf(err, n, "HUD element %d: %s", index, msg);
        else snprintf(err, n, "%s", msg);
    }
    return 0;
}

static int in_range(const HcHudPack* p, uint32_t off, size_t bytes) {
    return off <= p->size && bytes <= p->size - off;
}

static int texture_ok(const HcHudPack* p, unsigned t) {
    return t < (unsigned)p->texture_count;
}

static int check(HcHudPack* p, char* err, size_t n) {
    const PackHeader* h = (const PackHeader*)p->data;
    if (memcmp(h->magic, "HCHD", 4) != 0) return fail(err, n, "bad magic", -1);
    if (h->version != PACK_VERSION) return fail(err, n, "unsupported version (re-run tools/import_halo.py)", -1);
    if (h->texture_count > 0xFFFF || h->element_count > 0xFFFF || h->cell_count > 0xFFFF)
        return fail(err, n, "too many entries", -1);
    if ((h->textures_offset & 3) || (h->elements_offset & 3) || (h->cells_offset & 3) ||
        !in_range(p, h->textures_offset, (size_t)h->texture_count * sizeof(HcHudTexture)) ||
        !in_range(p, h->elements_offset, (size_t)h->element_count * sizeof(HcHudElement)) ||
        !in_range(p, h->cells_offset, (size_t)h->cell_count * sizeof(HcHudCell)))
        return fail(err, n, "tables out of range", -1);
    p->texture_count = (int)h->texture_count;
    p->element_count = (int)h->element_count;
    p->cell_count = (int)h->cell_count;
    p->textures = (const HcHudTexture*)(p->data + h->textures_offset);
    p->elements = (const HcHudElement*)(p->data + h->elements_offset);
    p->cells = (const HcHudCell*)(p->data + h->cells_offset);

    for (int i = 0; i < p->texture_count; i++) {
        const HcHudTexture* t = &p->textures[i];
        if (t->width == 0 || t->height == 0 || (t->width & 3) || (t->height & 3) || t->width > 1024 ||
            t->height > 1024 || (t->offset & 31) || !in_range(p, t->offset, (size_t)t->width * t->height * 4))
            return fail(err, n, "texture out of range", -1);
    }
    for (int i = 0; i < p->cell_count; i++) {
        const HcHudCell* c = &p->cells[i];
        if (!texture_ok(p, c->texture)) return fail(err, n, "meter cell texture out of range", -1);
        if (c->columns && !in_range(p, c->columns_offset, c->columns))
            return fail(err, n, "meter columns out of range", -1);
    }
    for (int i = 0; i < p->element_count; i++) {
        const HcHudElement* e = &p->elements[i];
        if (e->kind >= HC_HUD_KIND_COUNT || e->group >= HC_HUD_GROUP_COUNT || e->anchor > HC_HUD_CENTRE)
            return fail(err, n, "bad kind, group or anchor", i);
        if (e->texture != HC_HUD_NO_TEXTURE && !texture_ok(p, e->texture)) return fail(err, n, "bad texture", i);
        if ((int)e->first_cell + e->cell_count > p->cell_count) return fail(err, n, "cells out of range", i);
        if (e->kind == HC_HUD_METER) {
            if (e->cell_count == 0) return fail(err, n, "meter without cells", i);
            if ((e->flags & HC_HUD_METER_CONTINUOUS) &&
                (e->cell_count != 1 || p->cells[e->first_cell].columns == 0))
                return fail(err, n, "continuous meter without columns", i);
        }
    }
    return 1;
}

int hc_hud_pack_load(HcHudPack* p, const char* path, char* err, size_t n) {
    memset(p, 0, sizeof(*p));
    FILE* f = fopen(path, "rb");
    if (f == NULL) return fail(err, n, "not found", -1);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < (long)sizeof(PackHeader)) {
        fclose(f);
        return fail(err, n, "truncated", -1);
    }
    /* Textures are 32-byte aligned within the file; keep that in memory. */
    p->allocation = malloc((size_t)size + 31);
    if (p->allocation == NULL) {
        fclose(f);
        return fail(err, n, "out of memory", -1);
    }
    p->data = (const uint8_t*)(((uintptr_t)p->allocation + 31) & ~(uintptr_t)31);
    p->size = (size_t)size;
    size_t got = fread((void*)p->data, 1, p->size, f);
    fclose(f);
    if (got != p->size ? fail(err, n, "read error", -1) : !check(p, err, n)) {
        hc_hud_pack_free(p);
        return 0;
    }
    p->loaded = 1;
    return 1;
}

void hc_hud_pack_free(HcHudPack* p) {
    free(p->allocation);
    memset(p, 0, sizeof(*p));
}

const void* hc_hud_texture_pixels(const HcHudPack* p, int texture) {
    if (p == NULL || !p->loaded || texture < 0 || texture >= p->texture_count) return NULL;
    return p->data + p->textures[texture].offset;
}

const uint8_t* hc_hud_cell_columns(const HcHudPack* p, const HcHudCell* c) {
    if (p == NULL || !p->loaded || c == NULL || c->columns == 0) return NULL;
    return p->data + c->columns_offset;
}

const HcHudElement* hc_hud_find(const HcHudPack* p, int kind, int group, int state) {
    if (p == NULL || !p->loaded) return NULL;
    for (int i = 0; i < p->element_count; i++) {
        const HcHudElement* e = &p->elements[i];
        if (e->kind == kind && e->group == group && (state < 0 || e->state == state)) return e;
    }
    return NULL;
}

float hc_hud_meter_value(const HcHudElement* e, float value) {
    if (!(value > 0.0f)) return 0.0f;
    if (value > 1.0f) value = 1.0f;
    float min = (float)(e->flags & 0xFF) / 255.0f;
    return value < min ? min : value;
}

int hc_hud_ticks_lit(const HcHudElement* e, float value) {
    float v = hc_hud_meter_value(e, value);
    int lit = (int)ceilf(v * (float)e->cell_count - 0.001f);
    return lit < 0 ? 0 : lit > e->cell_count ? e->cell_count : lit;
}

int hc_hud_column_lit(uint8_t level, float value) {
    if (level == HC_HUD_COLUMN_EMPTY || !(value > 0.0f)) return 0;
    return level <= (int)(value * 254.0f + 0.5f);
}
