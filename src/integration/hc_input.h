/* InputAdapter: SDL keyboard/mouse -> Halo unit control, with Halo PC's
 * default bindings plus a full arsenal: wheel cycles, 1..0 select, Tab/Q
 * swaps to the last weapon, RMB/F throws, G switches grenades, Z/MMB zooms.
 * Only active (and only capturing the mouse) in the first-person mode;
 * Animal Crossing keeps its own pad mapping otherwise. */
#ifndef HC_INPUT_H
#define HC_INPUT_H

#include "halo/halo_sim.h"

union SDL_Event;

typedef struct HcInput {
    int capture;            /* mouse captured, Halo keys live */
    float mouse_dx, mouse_dy;
    int lmb, rmb;
    int jump, reload, swap, grenade;   /* latched presses */
    int cycle;              /* latched wheel steps, +next / -previous */
    int select;             /* latched weapon id + 1 from the number row */
    int grenade_cycle, zoom;
    int fkey_pressed[13];   /* F1..F12, latched */
    float sensitivity;      /* radians per mouse count */
    float sens_scale;       /* host-set, e.g. 1/zoom while scoped */
    int invert_y;
    int settle_frames;   /* ignore mouse motion for N applies after capture */
} HcInput;

void hc_input_init(HcInput* in);

/* Returns nonzero when the event belongs to the Halo layer. */
int hc_input_event(HcInput* in, const union SDL_Event* e);

/* Applies look + movement to `c`. Presses OR into the control latches so a
 * frame without a sim tick doesn't drop them. */
void hc_input_apply(HcInput* in, HaloUnitControl* c);

void hc_input_set_capture(HcInput* in, int on);
int hc_input_take_fkey(HcInput* in, int n);
int hc_input_take_zoom(HcInput* in);

#endif
