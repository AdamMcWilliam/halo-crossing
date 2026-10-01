/* Gameplay definitions for the Halo sandbox. These are trimmed-down mirrors of
 * Halo CE tag structs (weapon/projectile/damage_effect/biped/actor). Field
 * names follow the tag layouts in punpckhdq/halo `source/items/
 * weapon_definitions.h` so a later tag importer can fill them one-to-one.
 *
 * Units: world units (1 wu = 10 ft = 3.048 m), seconds, radians.
 * Velocities are per second (Halo stores them per tick; see halo_units.h). */
#ifndef HALO_DEFS_H
#define HALO_DEFS_H

typedef enum HaloDamageCategory {
    HALO_DAMAGE_BULLET,
    HALO_DAMAGE_PLASMA,
    HALO_DAMAGE_PLASMA_OVERCHARGE, /* charged plasma pistol bolt: strips shields */
    HALO_DAMAGE_EXPLOSION,
    HALO_DAMAGE_MELEE,
    HALO_DAMAGE_FALL,
    HALO_DAMAGE_FIRE,
    HALO_DAMAGE_NEEDLE,            /* weak against shields, nasty against flesh */
    HALO_DAMAGE_CATEGORY_COUNT
} HaloDamageCategory;

typedef struct HaloDamageEffectDef {
    HaloDamageCategory category;
    float damage_lower_bound;
    float damage_upper_bound;
    float radius_inner;              /* area damage: full damage inside */
    float radius_outer;              /* area damage: falls to zero at this radius */
    float instantaneous_acceleration; /* knockback, wu/s */
    int precision;                   /* head hits apply head_damage_multiplier */
} HaloDamageEffectDef;

typedef enum HaloProjectileId {
    HALO_PROJ_AR_BULLET,
    HALO_PROJ_PISTOL_BULLET,
    HALO_PROJ_SHOTGUN_PELLET,
    HALO_PROJ_SNIPER_BULLET,
    HALO_PROJ_ROCKET,
    HALO_PROJ_FLAME,
    HALO_PROJ_PLASMA_PISTOL_BOLT,
    HALO_PROJ_PLASMA_PISTOL_CHARGED,
    HALO_PROJ_PLASMA_RIFLE_BOLT,
    HALO_PROJ_NEEDLE,
    HALO_PROJ_FUEL_ROD,
    HALO_PROJ_PLASMA_GRENADE,
    HALO_PROJ_FRAG_GRENADE,
    HALO_PROJ_COUNT
} HaloProjectileId;

/* How placeholder art draws a projectile (hosts may ignore it). */
typedef enum HaloRenderStyle {
    HALO_RENDER_TRACER,
    HALO_RENDER_BOLT,
    HALO_RENDER_GRENADE,
    HALO_RENDER_ROCKET,
    HALO_RENDER_FLAME,
    HALO_RENDER_NEEDLE,
} HaloRenderStyle;

typedef struct HaloProjectileDef {
    const char* name;
    float initial_velocity;
    float final_velocity;
    float air_gravity_scale;
    float maximum_range;             /* projectile ages out past this distance */
    float guided_angular_velocity;   /* rad/s; > 0 means it tracks its target */
    int attaches_to_units;           /* plasma grenade stick */
    int attaches_to_world;
    float detonation_timer;          /* seconds after arming (thrown); 0 = on impact */
    float detonation_timer_attached; /* seconds after sticking */
    float bounce_restitution;        /* > 0: bounces off the world and units (frags) */
    HaloDamageEffectDef impact_damage; /* also applied when sticking to a unit */
    HaloDamageEffectDef detonation_damage; /* hits the carrier; area damage when radius > 0 */
    int supercombine_count;          /* needler: this many stuck in one unit detonate together */
    HaloDamageEffectDef supercombine_damage;
    HaloRenderStyle render_style;
    unsigned int render_rgba;        /* tracer/bolt color for placeholder art */
    float render_size;               /* wu */
} HaloProjectileDef;

/* Scroll order: human weapons, then Covenant. */
typedef enum HaloWeaponId {
    HALO_WEAPON_NONE = -1,
    HALO_WEAPON_ASSAULT_RIFLE = 0,
    HALO_WEAPON_PISTOL,
    HALO_WEAPON_SHOTGUN,
    HALO_WEAPON_SNIPER_RIFLE,
    HALO_WEAPON_ROCKET_LAUNCHER,
    HALO_WEAPON_FLAMETHROWER,
    HALO_WEAPON_PLASMA_PISTOL,
    HALO_WEAPON_PLASMA_RIFLE,
    HALO_WEAPON_NEEDLER,
    HALO_WEAPON_FUEL_ROD,
    HALO_WEAPON_COUNT
} HaloWeaponId;

typedef enum HaloGrenadeId {
    HALO_GRENADE_PLASMA,
    HALO_GRENADE_FRAG,
    HALO_GRENADE_COUNT
} HaloGrenadeId;

typedef struct HaloWeaponTriggerDef {
    float initial_rate_of_fire;      /* rounds/s when the trigger is first pulled */
    float final_rate_of_fire;        /* rounds/s after rate_of_fire_acceleration_time */
    float rate_of_fire_acceleration_time;
    int automatic;                   /* hold to keep firing */
    int rounds_per_shot;
    int projectiles_per_shot;
    float error_acceleration_time;   /* seconds of fire to reach final error */
    float error_deceleration_time;   /* seconds of rest to return to initial error */
    float projectile_error_angle_lower_bound; /* radians at error fraction 0 */
    float projectile_error_angle_upper_bound; /* radians at error fraction 1 */
    float heat_generated_per_round;
    float charging_time;             /* > 0: hold to charge, release fires charged projectile */
    float charged_heat;
    HaloProjectileId projectile;
    HaloProjectileId charged_projectile;
} HaloWeaponTriggerDef;

typedef struct HaloWeaponDef {
    const char* name;
    const char* hud_name;
    int rounds_loaded_maximum;       /* magazine size; 0 = energy weapon */
    int rounds_total_initial;        /* reserve on pickup */
    int rounds_total_maximum;
    float reload_time;               /* per round when reload_per_round (shotgun) */
    int reload_per_round;            /* shells go in one at a time; firing interrupts */
    float ready_time;                /* switch-to time */
    float heat_recovery_threshold;   /* overheated weapon can fire again below this */
    float heat_overheated_threshold;
    float heat_loss_per_second;
    float overheated_vent_time;      /* forced vent duration */
    float battery_per_round;         /* energy weapons: 0..1 drained per shot */
    float autoaim_angle;             /* aim assist cone, radians */
    float magnetism_range;
    int zoom_levels;
    float zoom_magnification[2];
    HaloWeaponTriggerDef trigger;
    /* first-person placeholder pose relative to the eye, wu (forward, left, up) */
    float fp_offset[3];
} HaloWeaponDef;

typedef struct HaloGrenadeDef {
    const char* name;
    HaloProjectileId projectile;
    float throw_velocity;            /* unit.grenade_velocity, wu/s */
    float throw_pitch_bias;          /* radians added above the aim vector */
    float throw_delay;               /* seconds from button to release */
    int maximum_count;
} HaloGrenadeDef;

typedef enum HaloBipedId {
    HALO_BIPED_CYBORG,   /* Master Chief */
    HALO_BIPED_GRUNT,
    HALO_BIPED_ELITE,
    HALO_BIPED_VILLAGER, /* hit volume for a host-driven Animal Crossing NPC */
    HALO_BIPED_COUNT
} HaloBipedId;

typedef struct HaloBipedDef {
    const char* name;
    float run_forward_speed;
    float run_backward_speed;
    float run_sideways_speed;
    float crouch_speed_scale;
    float run_acceleration;          /* wu/s^2 (tag value * TICKS_PER_SECOND) */
    float airborne_acceleration;     /* wu/s^2 */
    float jump_velocity;             /* wu/s */
    float standing_camera_height;
    float crouching_camera_height;
    float standing_collision_height;
    float crouching_collision_height;
    float collision_radius;
    float head_height_fraction;      /* hit z above this fraction of height counts as head */
    /* collision model / damage resistance */
    float maximum_body_vitality;
    float maximum_shield_vitality;   /* 0 = no shields */
    float shield_stun_time;          /* seconds after damage before recharge begins */
    float shield_recharge_time;      /* seconds from empty to full */
    float head_damage_multiplier;
    float shield_damage_multiplier[HALO_DAMAGE_CATEGORY_COUNT];
    float body_damage_multiplier[HALO_DAMAGE_CATEGORY_COUNT];
} HaloBipedDef;

typedef enum HaloActorTypeId {
    HALO_ACTOR_GRUNT,
    HALO_ACTOR_ELITE,
    HALO_ACTOR_TYPE_COUNT
} HaloActorTypeId;

/* actor + actor_variant tag essentials */
typedef struct HaloActorDef {
    const char* name;
    HaloBipedId biped;
    HaloWeaponId weapon;
    int plasma_grenades;
    float vision_range;
    float vision_half_angle;         /* radians */
    float hearing_range;             /* gunfire / explosions */
    float acknowledge_time;          /* surprise delay before engaging */
    float combat_range_min;          /* preferred firing distance band */
    float combat_range_max;
    float burst_duration_min, burst_duration_max;
    float burst_separation_min, burst_separation_max;
    float aim_error_angle;           /* radians, added on top of weapon error */
    float semiauto_fire_rate;        /* trigger pulls/s with non-automatic weapons */
    float movement_speed_scale;
    float strafe_chance;             /* per second while in combat */
    float panic_chance_leader_killed;
    float panic_chance_friend_killed;
    float flee_body_fraction;        /* below this body vitality fraction, consider fleeing */
    float flee_time_min, flee_time_max;
    float grenade_chance;            /* per second while target is in range band */
    int has_melee;
    float melee_range;
    float melee_damage;
    int is_leader;                   /* killing a leader can panic its squad */
} HaloActorDef;

extern const HaloProjectileDef g_halo_projectiles[HALO_PROJ_COUNT];
extern const HaloWeaponDef g_halo_weapons[HALO_WEAPON_COUNT];
extern const HaloGrenadeDef g_halo_grenades[HALO_GRENADE_COUNT];
extern const HaloBipedDef g_halo_bipeds[HALO_BIPED_COUNT];
extern const HaloActorDef g_halo_actors[HALO_ACTOR_TYPE_COUNT];

#endif
