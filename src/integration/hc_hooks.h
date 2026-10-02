/* The only interface the Animal Crossing host calls into. Every call site in
 * the host is wrapped in #ifdef HALO_CROSSING (patches/host/). */
#ifndef HC_HOOKS_H
#define HC_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

struct game_play_s;
struct actor_s;
struct PADStatus;
union SDL_Event;

void hc_hook_play_init(struct game_play_s* play);
void hc_hook_play_cleanup(struct game_play_s* play);
/* After all actors have moved this frame. */
void hc_hook_play_update(struct game_play_s* play);
/* After the AC camera has written play->view. */
void hc_hook_camera(struct game_play_s* play);
/* After actors are drawn (POLY_OPA / POLY_XLU still open). */
void hc_hook_draw_world(struct game_play_s* play);
/* After the whole play frame is drawn (FONT layer). */
void hc_hook_draw_hud(struct game_play_s* play);
/* Returns nonzero if the event was consumed. */
int hc_hook_sdl_event(const union SDL_Event* e);
/* Last chance to edit pad 0 before the game sees it. */
void hc_hook_filter_pad(struct PADStatus* status);
/* Nonzero: Nook's shops ignore their opening hours. */
int hc_hook_shops_never_close(void);
/* A message window just loaded message msg_no for speaker (may be NULL) into
 * text (cap bytes). Returns the new length to replace it, or 0 to keep it. */
int hc_hook_message(struct actor_s* speaker, int msg_no, unsigned char* text, int cap);

#ifdef __cplusplus
}
#endif

#endif
