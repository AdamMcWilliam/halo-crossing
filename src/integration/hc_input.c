#include "hc_input.h"

#include <SDL.h>
#include <string.h>

void hc_input_init(HcInput* in) {
    memset(in, 0, sizeof(*in));
    in->sensitivity = 0.0022f;
    in->sens_scale = 1.0f;
}

static void clear_presses(HcInput* in) {
    in->jump = in->reload = in->swap = in->grenade = 0;
    in->cycle = in->select = in->grenade_cycle = 0;
}

void hc_input_set_capture(HcInput* in, int on) {
    on = on ? 1 : 0;
    if (in->capture == on) return;
    in->capture = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    in->mouse_dx = in->mouse_dy = 0.0f;
    in->lmb = in->rmb = 0;
    in->zoom = 0;
    clear_presses(in);
    /* Entering relative mode can report the cursor's jump to the window
     * centre as one huge motion; drop the first frames. */
    in->settle_frames = on ? 3 : 0;
}

int hc_input_take_fkey(HcInput* in, int n) {
    if (n < 1 || n > 12 || !in->fkey_pressed[n]) return 0;
    in->fkey_pressed[n] = 0;
    return 1;
}

int hc_input_take_zoom(HcInput* in) {
    int z = in->zoom;
    in->zoom = 0;
    return z;
}

/* 1..9 then 0 pick weapons 1..10, matching the scroll order. */
static int number_key_weapon(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return 1 + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return 10;
    return 0;
}

static int is_halo_key(SDL_Scancode sc) {
    if (number_key_weapon(sc)) return 1;
    switch (sc) {
        case SDL_SCANCODE_W:
        case SDL_SCANCODE_A:
        case SDL_SCANCODE_S:
        case SDL_SCANCODE_D:
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_C:
        case SDL_SCANCODE_R:
        case SDL_SCANCODE_TAB:
        case SDL_SCANCODE_Q:
        case SDL_SCANCODE_G:
        case SDL_SCANCODE_F:
        case SDL_SCANCODE_Z:
        case SDL_SCANCODE_LSHIFT:
            return 1;
        default:
            return 0;
    }
}

int hc_input_event(HcInput* in, const union SDL_Event* ev) {
    const SDL_Event* e = (const SDL_Event*)ev;
    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            SDL_Scancode sc = e->key.keysym.scancode;
            if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
                int n = sc <= SDL_SCANCODE_F10 ? 1 + (sc - SDL_SCANCODE_F1) : 11 + (sc - SDL_SCANCODE_F11);
                if (e->type == SDL_KEYDOWN && !e->key.repeat) in->fkey_pressed[n] = 1;
                return 1;
            }
            if (sc == SDL_SCANCODE_ESCAPE && e->type == SDL_KEYDOWN) {
                /* Let the port open its pause menu with a free cursor. */
                hc_input_set_capture(in, 0);
                return 0;
            }
            if (!in->capture || !is_halo_key(sc)) return 0;
            if (e->type == SDL_KEYDOWN && !e->key.repeat) {
                if (sc == SDL_SCANCODE_SPACE) in->jump = 1;
                if (sc == SDL_SCANCODE_R) in->reload = 1;
                if (sc == SDL_SCANCODE_TAB || sc == SDL_SCANCODE_Q) in->swap = 1;
                if (sc == SDL_SCANCODE_F) in->grenade = 1;
                if (sc == SDL_SCANCODE_G) in->grenade_cycle = 1;
                if (sc == SDL_SCANCODE_Z) in->zoom = 1;
                int w = number_key_weapon(sc);
                if (w) in->select = w;
            }
            return 1;
        }
        case SDL_MOUSEMOTION:
            if (!in->capture) return 0;
            in->mouse_dx += (float)e->motion.xrel;
            in->mouse_dy += (float)e->motion.yrel;
            return 1;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (!in->capture) return 0;
            if (e->button.button == SDL_BUTTON_LEFT) in->lmb = e->type == SDL_MOUSEBUTTONDOWN;
            if (e->button.button == SDL_BUTTON_RIGHT) {
                in->rmb = e->type == SDL_MOUSEBUTTONDOWN;
                if (in->rmb) in->grenade = 1;
            }
            if (e->button.button == SDL_BUTTON_MIDDLE && e->type == SDL_MOUSEBUTTONDOWN) in->zoom = 1;
            return 1;
        case SDL_MOUSEWHEEL: {
            if (!in->capture) return 0;
            int y = e->wheel.y;
            if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
            /* Wheel down = next weapon. */
            if (y < 0) in->cycle++;
            if (y > 0) in->cycle--;
            return 1;
        }
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) hc_input_set_capture(in, 0);
            return 0;
        default:
            return 0;
    }
}

void hc_input_apply(HcInput* in, HaloUnitControl* c) {
    if (!in->capture) {
        c->throttle_forward = c->throttle_left = 0.0f;
        c->fire = c->crouch = 0;
        clear_presses(in);
        return;
    }
    const Uint8* k = SDL_GetKeyboardState(NULL);
    float f = 0.0f, l = 0.0f;
    if (k[SDL_SCANCODE_W]) f += 1.0f;
    if (k[SDL_SCANCODE_S]) f -= 1.0f;
    if (k[SDL_SCANCODE_A]) l += 1.0f;
    if (k[SDL_SCANCODE_D]) l -= 1.0f;
    c->throttle_forward = f;
    c->throttle_left = l;
    c->crouch = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_C];
    c->fire = in->lmb;

    if (in->settle_frames > 0) {
        in->settle_frames--;
        in->mouse_dx = in->mouse_dy = 0.0f;
    }
    float sens = in->sensitivity * (in->sens_scale > 0.0f ? in->sens_scale : 1.0f);
    c->aim_yaw = hc_wrap_angle(c->aim_yaw - in->mouse_dx * sens);
    float dy = in->mouse_dy * sens * (in->invert_y ? -1.0f : 1.0f);
    c->aim_pitch = hc_clampf(c->aim_pitch - dy, HC_DEG2RAD(-85.0f), HC_DEG2RAD(85.0f));
    in->mouse_dx = in->mouse_dy = 0.0f;

    c->jump_pressed |= in->jump;
    c->reload_pressed |= in->reload;
    c->swap_pressed |= in->swap;
    c->grenade_pressed |= in->grenade;
    c->grenade_cycle_pressed |= in->grenade_cycle;
    /* One weapon step per tick: several wheel clicks in one frame still move one slot each. */
    if (in->cycle != 0 && c->weapon_cycle == 0) {
        c->weapon_cycle = in->cycle > 0 ? 1 : -1;
        in->cycle += in->cycle > 0 ? -1 : 1;
    }
    if (in->select) c->weapon_select = in->select;
    in->jump = in->reload = in->swap = in->grenade = 0;
    in->select = in->grenade_cycle = 0;
}
