/* HUD pack loader and meter checks on a synthetic pack; the imported pack
 * (tools/import_halo.py) is checked when present. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "integration/hc_hud_pack.h"

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                               \
        }                                                               \
    } while (0)

enum { TEX_OFF = 32, ELEM_OFF = 40, CELL_OFF = 160, COL_OFF = 224, PIX_OFF = 256, PACK_SIZE = 320 };

/* One 4x4 texture; a static, a three-tick meter and an 8-column continuous meter. */
static void build_pack(unsigned char* d) {
    memset(d, 0, PACK_SIZE);
    memcpy(d, "HCHD", 4);
    uint32_t head[7] = { 1, 1, TEX_OFF, 3, ELEM_OFF, 4, CELL_OFF };
    memcpy(d + 4, head, sizeof(head));
    HcHudTexture t = { 4, 4, PIX_OFF };
    memcpy(d + TEX_OFF, &t, sizeof(t));
    HcHudElement e[3] = {
        { HC_HUD_STATIC, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD, HC_HUD_TOP_RIGHT, -40, 4, 4, 4, 0, 0, 0, 0, { 0x2896FFFF } },
        { HC_HUD_METER, HC_HUD_GROUP_WEAPON(1), HC_HUD_LOADED_AMMO, HC_HUD_TOP_LEFT, 4, 20, 12, 4, HC_HUD_NO_TEXTURE,
          0, 3, 0, { 0 } },
        { HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD, HC_HUD_TOP_RIGHT, -40, 0, 8, 4, HC_HUD_NO_TEXTURE, 3, 1,
          HC_HUD_METER_CONTINUOUS | 31, { 0 } },
    };
    memcpy(d + ELEM_OFF, e, sizeof(e));
    HcHudCell c[4] = { { 0, 0, 0, 0, 0, 0 }, { 0, 4, 0, 0, 0, 0 }, { 0, 8, 0, 0, 0, 0 }, { 0, 0, 0, 8, COL_OFF, 0 } };
    memcpy(d + CELL_OFF, c, sizeof(c));
    const uint8_t cols[8] = { 254, 200, 150, 100, 50, 0, 255, 255 };
    memcpy(d + COL_OFF, cols, sizeof(cols));
}

static int write_and_load(HcHudPack* p, const char* dir, const char* name, const unsigned char* d, size_t size,
                          char* err, size_t n) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(d, 1, size, f);
        fclose(f);
    }
    return hc_hud_pack_load(p, path, err, n);
}

static void test_synthetic(const char* dir) {
    unsigned char d[PACK_SIZE];
    char err[128];
    HcHudPack p;
    build_pack(d);
    CHECK(write_and_load(&p, dir, "hud_ok.hcpk", d, sizeof(d), err, sizeof(err)));
    CHECK(p.loaded && p.element_count == 3 && p.cell_count == 4 && p.texture_count == 1);
    CHECK(((uintptr_t)hc_hud_texture_pixels(&p, 0) & 31) == 0);
    CHECK(hc_hud_texture_pixels(&p, 1) == NULL);

    const HcHudElement* ticks = hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_WEAPON(1), HC_HUD_LOADED_AMMO);
    CHECK(ticks != NULL && ticks->cell_count == 3);
    CHECK(hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_WEAPON(2), -1) == NULL);
    if (ticks) {
        CHECK(hc_hud_ticks_lit(ticks, 0.0f) == 0);
        CHECK(hc_hud_ticks_lit(ticks, 1.0f / 3.0f) == 1);
        CHECK(hc_hud_ticks_lit(ticks, 0.34f) == 2);
        CHECK(hc_hud_ticks_lit(ticks, 2.0f / 3.0f) == 2);
        CHECK(hc_hud_ticks_lit(ticks, 1.5f) == 3);
    }
    const HcHudElement* bar = hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD);
    CHECK(bar != NULL && (bar->flags & HC_HUD_METER_CONTINUOUS));
    if (bar) {
        const uint8_t* cols = hc_hud_cell_columns(&p, &p.cells[bar->first_cell]);
        CHECK(cols != NULL && cols[0] == 254 && cols[6] == HC_HUD_COLUMN_EMPTY);
        /* A sliver of shield still shows the meter's minimum (31/255). */
        CHECK(hc_hud_meter_value(bar, 0.01f) > 0.12f && hc_hud_meter_value(bar, 0.0f) == 0.0f);
        CHECK(!hc_hud_column_lit(0, 0.0f));
        CHECK(hc_hud_column_lit(0, 0.01f) && !hc_hud_column_lit(100, 0.3f) && hc_hud_column_lit(100, 0.5f));
        CHECK(hc_hud_column_lit(254, 1.0f) && !hc_hud_column_lit(HC_HUD_COLUMN_EMPTY, 1.0f));
    }
    hc_hud_pack_free(&p);
    CHECK(p.data == NULL && hc_hud_find(&p, HC_HUD_STATIC, 0, -1) == NULL);

    CHECK(!hc_hud_pack_load(&p, "no/such/hud.hcpk", err, sizeof(err)) && strstr(err, "not found"));
    build_pack(d);
    memcpy(d, "NOPE", 4);
    CHECK(!write_and_load(&p, dir, "hud_magic.hcpk", d, sizeof(d), err, sizeof(err)) && strstr(err, "magic"));
    CHECK(p.data == NULL);
    build_pack(d);
    HcHudTexture bad = { 4, 4, PIX_OFF + 4 };
    memcpy(d + TEX_OFF, &bad, sizeof(bad));
    CHECK(!write_and_load(&p, dir, "hud_align.hcpk", d, sizeof(d), err, sizeof(err)) && strstr(err, "texture"));
    build_pack(d);
    uint16_t many = 9;
    memcpy(d + ELEM_OFF + 40 + 16, &many, 2); /* the tick meter's cell_count */
    CHECK(!write_and_load(&p, dir, "hud_cells.hcpk", d, sizeof(d), err, sizeof(err)) && strstr(err, "cells"));
    build_pack(d);
    uint32_t cols_off = PACK_SIZE - 4;
    memcpy(d + CELL_OFF + 3 * 16 + 8, &cols_off, 4);
    CHECK(!write_and_load(&p, dir, "hud_cols.hcpk", d, sizeof(d), err, sizeof(err)) && strstr(err, "columns"));
    CHECK(!write_and_load(&p, dir, "hud_short.hcpk", d, 20, err, sizeof(err)) && strstr(err, "truncated"));
}

static void test_imported(const char* assets) {
    char path[512], err[128];
    HcHudPack p;
    snprintf(path, sizeof(path), "%s/%s", assets, HC_HUD_PACK_NAME);
    if (!hc_hud_pack_load(&p, path, err, sizeof(err))) {
        printf("hud_pack_test: imported pack skipped (%s)\n", err);
        return;
    }
    const HcHudElement* shield = hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_SHIELD);
    const HcHudElement* health = hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_UNIT, HC_HUD_HEALTH);
    CHECK(shield != NULL && (shield->flags & HC_HUD_METER_CONTINUOUS));
    CHECK(health != NULL && !(health->flags & HC_HUD_METER_CONTINUOUS) && health->cell_count == 8);
    CHECK(hc_hud_find(&p, HC_HUD_SENSOR, HC_HUD_GROUP_UNIT, -1) != NULL);
    for (int d = 0; d < 10; d++) CHECK(hc_hud_find(&p, HC_HUD_DIGIT, HC_HUD_GROUP_GLOBAL, d) != NULL);
    /* Magazine ticks per weapon: AR 60, pistol 12, shotgun 12, sniper 4, rocket 2, needler 20. */
    static const struct { int weapon, ticks; } mags[] = { { 0, 60 }, { 1, 12 }, { 2, 12 }, { 3, 4 }, { 4, 2 }, { 8, 20 } };
    for (size_t i = 0; i < sizeof(mags) / sizeof(mags[0]); i++) {
        const HcHudElement* m = hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_WEAPON(mags[i].weapon), HC_HUD_LOADED_AMMO);
        CHECK(m != NULL && m->cell_count == mags[i].ticks);
    }
    for (int w = 0; w < 9; w++) {
        if (w == 5) continue; /* the flamethrower's HUD has no reticle on the Xbox discs */
        CHECK(hc_hud_find(&p, HC_HUD_RETICLE, HC_HUD_GROUP_WEAPON(w), -1) != NULL);
    }
    CHECK(hc_hud_find(&p, HC_HUD_METER, HC_HUD_GROUP_WEAPON(6), HC_HUD_HEAT) != NULL);
    printf("hud_pack_test: imported pack has %d elements, %d cells, %d textures\n", p.element_count, p.cell_count,
           p.texture_count);
    hc_hud_pack_free(&p);
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    test_synthetic(dir);
    if (argc > 2) test_imported(argv[2]);
    if (g_failures) {
        fprintf(stderr, "hud_pack_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("hud_pack_test: ok\n");
    return 0;
}
