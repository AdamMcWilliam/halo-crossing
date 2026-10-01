/* Halo combat sandbox: units (bipeds), weapons, projectiles, damage, AI.
 * Runs at Halo's fixed 30 Hz tick in Halo world units. Hosts call
 * halo_sim_advance() once per rendered frame and read state for drawing,
 * using `alpha` to interpolate between the previous and current tick. */
#ifndef HALO_SIM_H
#define HALO_SIM_H

#include "halo_defs.h"
#include "halo_units.h"
#include "halo_world.h"
#include "hc_vec.h"

#define HALO_MAX_UNITS 96
#define HALO_MAX_PROJECTILES 384
#define HALO_MAX_EVENTS 256
#define HALO_AI_MAX_PATH 24

typedef enum HaloTeam {
    HALO_TEAM_HUMAN,
    HALO_TEAM_COVENANT,
    HALO_TEAM_NEUTRAL,        /* bystanders: anyone can hurt them, AI never targets them */
    HALO_TEAM_COUNT
} HaloTeam;

typedef struct HaloUnitControl {
    float throttle_forward;   /* -1..1 */
    float throttle_left;      /* -1..1 */
    float aim_yaw;            /* desired facing, radians */
    float aim_pitch;
    int fire;                 /* held */
    int crouch;               /* held */
    /* edge-triggered requests; latched until the next tick consumes them */
    int jump_pressed;
    int reload_pressed;
    int grenade_pressed;
    int swap_pressed;         /* back to the previous weapon (arsenal) or the holstered one */
    int weapon_cycle;         /* arsenal: +1 next / -1 previous */
    int weapon_select;        /* arsenal: weapon id + 1, 0 = none */
    int grenade_cycle_pressed;
} HaloUnitControl;

typedef struct HaloWeaponState {
    HaloWeaponId id;
    int rounds_loaded;
    int rounds_reserve;
    float battery;            /* energy weapons, 0..1 */
    float heat;               /* 0..1 */
    int overheated;
    float error;              /* 0..1 spread fraction */
    float fire_cooldown;      /* seconds until the next round may fire */
    float reload_timer;       /* > 0 while reloading */
    float ready_timer;        /* > 0 while drawing the weapon */
    float charge;             /* seconds the trigger has been held (charging weapons) */
    float trigger_time;       /* seconds held, drives rate-of-fire ramp */
    int trigger_was_down;
    float fire_flash;         /* render: muzzle flash, decays to 0 */
    float recoil;             /* render: viewmodel kick, decays to 0 */
} HaloWeaponState;

typedef enum HaloAiState {
    HALO_AI_IDLE,
    HALO_AI_ALERT,      /* heard something / acknowledging a target */
    HALO_AI_COMBAT,
    HALO_AI_SEARCH,
    HALO_AI_FLEE,
    HALO_AI_DEAD,
    HALO_AI_STATE_COUNT
} HaloAiState;

typedef struct HaloAi {
    int active;
    HaloActorTypeId type;
    HaloAiState state;
    float state_time;
    int target;               /* unit index or -1 */
    hv3 last_known_target;
    float target_visible_time;
    float target_lost_time;
    int target_visible;
    hv3 home;
    hv3 move_goal;
    int has_move_goal;
    float move_speed;         /* throttle magnitude toward the goal */
    hv3 path[HALO_AI_MAX_PATH];
    int path_count;
    int path_index;
    float repath_timer;
    float stuck_time;
    hv3 last_pos;
    float idle_timer;
    float burst_timer;        /* > 0: firing this burst */
    float burst_cooldown;     /* > 0: waiting between bursts */
    float semiauto_timer;
    float melee_cooldown;
    float strafe_timer;
    float strafe_dir;
    float flee_timer;
    float grenade_cooldown;
    float panic_timer;        /* render/villager use: flailing */
    int squad;
} HaloAi;

typedef struct HaloUnit {
    int active;
    int serial;               /* bumps on every spawn into this slot */
    HaloBipedId biped;
    HaloTeam team;
    int is_player;
    hv3 pos;                  /* feet */
    hv3 prev_pos;             /* previous tick, for interpolation */
    hv3 vel;                  /* wu/s */
    float yaw, pitch;         /* current facing/aim */
    float prev_yaw;
    int grounded;
    int crouching;
    float crouch_blend;       /* 0 stand .. 1 crouch, smoothed */
    float body;
    float shield;
    float shield_stun;        /* seconds before recharge may begin */
    int shield_recharging;
    int dead;
    float dead_time;
    float hurt_flash;         /* render: body hit */
    float shield_flash;       /* render: shield hit */
    hv3 last_damage_dir;      /* points from the unit toward the attacker */
    float last_damage_age;
    int last_attacker;
    HaloWeaponState weapon;
    HaloWeaponState holstered; /* second slot */
    int grenades[HALO_GRENADE_COUNT];
    HaloGrenadeId grenade_type; /* which kind the grenade button throws */
    float grenade_timer;      /* > 0: winding up a throw */
    HaloUnitControl control;
    float move_anim;          /* render: walk cycle phase */
    int kinematic;            /* host moves it (AC villagers); never recycled or removed */
} HaloUnit;

typedef struct HaloProjectile {
    int active;
    HaloProjectileId def;
    int owner;                /* unit index */
    HaloTeam team;
    hv3 pos;
    hv3 prev_pos;
    hv3 vel;
    hv3 visual_offset;        /* render: muzzle offset that decays toward 0 */
    float distance;
    float timer;              /* detonation countdown, seconds; < 0 = none */
    int attached_unit;        /* -1 none */
    int attached_serial;
    hv3 attach_offset;
    int stuck;                /* resting on/in the world */
    int target_unit;          /* guidance target, -1 none */
    float age;
} HaloProjectile;

typedef enum HaloEventType {
    HALO_EV_WEAPON_FIRED,     /* unit, pos, def = weapon id */
    HALO_EV_PROJECTILE_IMPACT,/* pos, dir = surface normal, def = projectile id, other = unit hit or -1 */
    HALO_EV_EXPLOSION,        /* pos, def = projectile id, other = carrier or -1, value = outer radius (0 = none) */
    HALO_EV_UNIT_DAMAGED,     /* unit, other = attacker, value = total damage */
    HALO_EV_SHIELD_DEPLETED,  /* unit */
    HALO_EV_SHIELD_RECHARGE,  /* unit */
    HALO_EV_UNIT_KILLED,      /* unit, other = killer */
    HALO_EV_RELOAD,           /* unit */
    HALO_EV_GRENADE_THROWN,   /* unit, pos */
    HALO_EV_GRENADE_STUCK,    /* pos, other = unit stuck to or -1 */
    HALO_EV_OVERHEAT,         /* unit */
    HALO_EV_AI_ALERTED,       /* unit */
    HALO_EV_AI_PANIC,         /* unit */
    HALO_EV_WEAPON_SWITCHED,  /* unit, def = new weapon id */
    HALO_EV_COUNT
} HaloEventType;

typedef struct HaloEvent {
    HaloEventType type;
    hv3 pos;
    hv3 dir;
    int unit;
    int other;
    int def;
    float value;
} HaloEvent;

typedef struct HaloSim {
    const HaloWorldApi* world;
    HaloUnit units[HALO_MAX_UNITS];
    HaloAi ai[HALO_MAX_UNITS];
    HaloProjectile projectiles[HALO_MAX_PROJECTILES];
    HaloEvent events[HALO_MAX_EVENTS];
    int event_count;
    int events_dropped;
    int player;               /* unit index of the player, -1 none */
    hv3 player_spawn;
    float player_spawn_yaw;
    float player_respawn_timer;
    unsigned int rng;
    unsigned int tick;
    float time;
    float accumulator;
    float alpha;              /* interpolation factor for rendering, 0..1 */
    int next_serial;
    /* The player's full arsenal: every weapon carried at once, scrolled
     * through instead of Halo's two-slot limit. Inactive slots live here. */
    int arsenal_enabled;
    int arsenal_owned[HALO_WEAPON_COUNT];
    HaloWeaponState arsenal[HALO_WEAPON_COUNT];
    HaloWeaponId arsenal_last;
    /* Idle AI farther than this from the player sleep (0 = never). */
    float ai_activation_range;
    int dormant_count;
    /* cheats / debug */
    int infinite_shields;
    int infinite_ammo;
    int ai_frozen;
} HaloSim;

void halo_sim_init(HaloSim* sim, const HaloWorldApi* world, unsigned int seed);

/* Runs as many fixed ticks as `dt` covers (capped), updating `alpha`.
 * Returns the number of ticks run. Events accumulate until cleared. */
int halo_sim_advance(HaloSim* sim, float dt);
void halo_sim_tick(HaloSim* sim);
void halo_sim_clear_events(HaloSim* sim);

int halo_spawn_player(HaloSim* sim, hv3 pos, float yaw);
int halo_spawn_actor(HaloSim* sim, HaloActorTypeId type, hv3 pos, float yaw);
void halo_kill_unit(HaloSim* sim, int unit, int killer);
/* `toward_attacker` is a unit vector from the victim toward the source. */
void halo_damage_unit(HaloSim* sim, int victim, int attacker, const HaloDamageEffectDef* e, float scale,
                      hv3 toward_attacker, int head);
void halo_remove_unit(HaloSim* sim, int unit);
void halo_give_weapon(HaloSim* sim, int unit, HaloWeaponId weapon);
/* Replaces the held weapon outright, ready to fire (squad loadouts). */
void halo_set_unit_weapon(HaloSim* sim, int unit, HaloWeaponId weapon);
/* Hands the player every weapon, full ammo and grenades, and keeps doing so on respawn. */
void halo_give_arsenal(HaloSim* sim);
/* A kinematic, AI-less unit the host positions every frame (pos/yaw). */
int halo_spawn_proxy(HaloSim* sim, HaloBipedId biped, HaloTeam team, hv3 pos, float yaw);
void halo_revive_unit(HaloSim* sim, int unit);
int halo_count_living(const HaloSim* sim, HaloTeam team);

HaloUnit* halo_player(HaloSim* sim);
hv3 halo_unit_eye(const HaloUnit* u);
hv3 halo_unit_eye_interp(const HaloUnit* u, float alpha);
hv3 halo_unit_pos_interp(const HaloUnit* u, float alpha);
float halo_unit_height(const HaloUnit* u);
const HaloBipedDef* halo_unit_def(const HaloUnit* u);

const char* halo_ai_state_name(HaloAiState s);

#endif
