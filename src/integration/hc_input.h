/* InputAdapter: SDL keyboard/mouse -> Halo unit control, with Halo PC's
 * default bindings. Only active (and only capturing the mouse) in the
 * first-person mode; Animal Crossing keeps its own pad mapping otherwise. */
#ifndef HC_INPUT_H
#define HC_INPUT_H

#include "halo/halo_sim.h"

union SDL_Event;

typedef struct HcInput {
    int capture;            /* mouse captured, Halo keys live */
    float mouse_dx, mouse_dy;
    int lmb, rmb;
    int jump, reload, swap, grenade;   /* latched presses */
    int fkey_pressed[13];   /* F1..F12, latched */
    float sensitivity;      /* radians per mouse count */
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

#endif
