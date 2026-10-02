/* Draws the Halo sandbox into the Animal Crossing frame: units, first-person
 * weapon, projectiles, effects (world pass) and the Halo HUD (overlay pass). */
#ifndef HC_DRAW_H
#define HC_DRAW_H

#include "halo/halo_sim.h"
#include "m_play.h"

#define HC_TRACKER_RANGE 8.2f /* wu, Halo CE's 25 m motion tracker */

typedef struct HcView {
    int first_person;
    xyz_t eye;            /* AC camera eye this frame */
    float yaw, pitch;     /* Halo aim, radians */
    float bob_phase;
    float bob_amount;     /* 0..1 */
    int show_collision;
    int show_ai;
    int show_nav;
    float time;
    float zoom;           /* scope magnification, 1 = unzoomed */
    float weapon_switch_age; /* seconds since the last weapon switch (shows the list) */
} HcView;

#define HC_DEBUG_LINES 8
#define HC_MAX_LABELS 16

/* Text floating over something in the world (villager shouts). */
typedef struct HcWorldLabel {
    xyz_t pos;            /* AC world position */
    char text[48];
    float alpha;
    u32 rgb;              /* 0xRRGGBB00 */
} HcWorldLabel;

typedef struct HcHudText {
    char lines[HC_DEBUG_LINES][96];
    int count;
    char center[64];      /* big centered message, "" = none */
    float center_alpha;
    HcWorldLabel labels[HC_MAX_LABELS];
    int label_count;
} HcHudText;

void hc_draw_world(GAME_PLAY* play, HaloSim* sim, const HcView* view);
void hc_draw_hud(GAME_PLAY* play, HaloSim* sim, const HcView* view, const HcHudText* text);

/* AC never shows the sky from its fixed camera; paint one behind the world.
 * Must run before the frame's BG pass (from the camera hook). */
void hc_draw_sky(GAME_PLAY* play, const HcView* view, float fov_y_deg);

/* Visual effects driven by sandbox events. */
void hc_fx_from_event(HaloSim* sim, const HaloEvent* e);
void hc_fx_update(float dt);
void hc_fx_clear(void);
/* A shower of gold bells (AC world position). */
void hc_fx_bells(xyz_t pos);

#endif
