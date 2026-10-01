/* The one table of Halo gameplay numbers.
 *
 * Provenance key used below:
 *   [engine]  compiled into Halo CE; cited from the halocea reconstruction.
 *   [approx]  hand-tuned to match how Halo CE plays; the real value lives in
 *             tag data (.map). Replace via tag import (tools/, later).
 *
 * Everything is in Halo world units / seconds / radians. Conversion into the
 * Animal Crossing world happens only in src/integration/hc_world_scale.h. */
#include "halo_defs.h"
#include "hc_vec.h"
#include "halo_units.h"

#define D2R HC_DEG2RAD

/*                                   bullet plasma overch explo  melee  fall */
#define SHIELD_MULT_STANDARD        { 0.75f, 1.50f, 6.00f, 1.00f, 1.00f, 0.00f }
#define BODY_MULT_STANDARD          { 1.00f, 0.80f, 0.50f, 1.00f, 1.00f, 1.00f }
#define BODY_MULT_UNSHIELDED        { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }

const HaloProjectileDef g_halo_projectiles[HALO_PROJ_COUNT] = {
    [HALO_PROJ_AR_BULLET] = {
        .name = "assault rifle bullet",
        .initial_velocity = 200.0f, .final_velocity = 200.0f, /* [approx] */
        .air_gravity_scale = 0.0f,
        .maximum_range = 60.0f,
        .impact_damage = { HALO_DAMAGE_BULLET, 7.0f, 7.5f, 0, 0, 0.25f, 0 }, /* [approx] */
        .render_rgba = 0xFFE27AFF, .render_size = 0.02f,
    },
    [HALO_PROJ_PLASMA_PISTOL_BOLT] = {
        .name = "plasma pistol bolt",
        .initial_velocity = 22.0f, .final_velocity = 22.0f, /* [approx] slow and visible */
        .air_gravity_scale = 0.0f,
        .maximum_range = 30.0f,
        .impact_damage = { HALO_DAMAGE_PLASMA, 7.0f, 8.0f, 0, 0, 0.2f, 0 }, /* [approx] */
        .render_rgba = 0x7CFF6AFF, .render_size = 0.05f,
    },
    [HALO_PROJ_PLASMA_PISTOL_CHARGED] = {
        .name = "plasma pistol overcharge",
        .initial_velocity = 16.0f, .final_velocity = 16.0f, /* [approx] */
        .air_gravity_scale = 0.0f,
        .maximum_range = 30.0f,
        .guided_angular_velocity = D2R(100.0f), /* [approx] tracks its target */
        .impact_damage = { HALO_DAMAGE_PLASMA_OVERCHARGE, 18.0f, 20.0f, 0, 0, 1.0f, 0 }, /* [approx] */
        .render_rgba = 0xB4FF9CFF, .render_size = 0.11f,
    },
    [HALO_PROJ_PLASMA_GRENADE] = {
        .name = "plasma grenade",
        .initial_velocity = 0.0f, .final_velocity = 0.0f, /* set by the throw */
        .air_gravity_scale = 1.0f,
        .maximum_range = 1000.0f,
        .attaches_to_units = 1, .attaches_to_world = 1,
        .detonation_timer = 3.0f,          /* [approx] armed when thrown */
        .detonation_timer_attached = 1.6f, /* [approx] after it sticks */
        .detonation_damage = { HALO_DAMAGE_EXPLOSION, 100.0f, 120.0f, 0.35f, 1.6f, 2.2f, 0 }, /* [approx] */
        .render_rgba = 0x6CB6FFFF, .render_size = 0.06f,
    },
};

const HaloWeaponDef g_halo_weapons[HALO_WEAPON_COUNT] = {
    [HALO_WEAPON_ASSAULT_RIFLE] = {
        .name = "MA5B Assault Rifle", .hud_name = "ASSAULT RIFLE",
        .rounds_loaded_maximum = 60,        /* CE magazine */
        .rounds_total_initial = 180,
        .rounds_total_maximum = 600,
        .reload_time = 2.2f,                /* [approx] */
        .ready_time = 0.5f,
        .autoaim_angle = D2R(2.0f), .magnetism_range = 15.0f,
        .trigger = {
            .initial_rate_of_fire = 15.0f, .final_rate_of_fire = 15.0f, /* ~900 rpm */
            .automatic = 1, .rounds_per_shot = 1, .projectiles_per_shot = 1,
            .error_acceleration_time = 0.9f, .error_deceleration_time = 0.35f, /* [approx] */
            .projectile_error_angle_lower_bound = D2R(0.5f),  /* [approx] */
            .projectile_error_angle_upper_bound = D2R(6.0f),  /* [approx] the CE bloom */
            .projectile = HALO_PROJ_AR_BULLET,
            .charged_projectile = HALO_PROJ_AR_BULLET,
        },
        .fp_offset = { 0.11f, -0.055f, -0.07f },
    },
    [HALO_WEAPON_PLASMA_PISTOL] = {
        .name = "Plasma Pistol", .hud_name = "PLASMA PISTOL",
        .rounds_loaded_maximum = 0,         /* energy weapon */
        .heat_recovery_threshold = 0.35f,   /* [approx] */
        .heat_overheated_threshold = 1.0f,
        .heat_loss_per_second = 0.45f,      /* [approx] */
        .battery_per_round = 0.004f,        /* [approx] */
        .ready_time = 0.4f,
        .autoaim_angle = D2R(2.5f), .magnetism_range = 12.0f,
        .trigger = {
            .initial_rate_of_fire = 6.0f, .final_rate_of_fire = 6.0f, /* [approx] click rate cap */
            .automatic = 0, .rounds_per_shot = 1, .projectiles_per_shot = 1,
            .error_acceleration_time = 0.3f, .error_deceleration_time = 0.3f,
            .projectile_error_angle_lower_bound = D2R(0.5f),
            .projectile_error_angle_upper_bound = D2R(2.0f),
            .heat_generated_per_round = 0.07f,  /* [approx] */
            .charging_time = 0.75f,             /* [approx] */
            .charged_heat = 1.0f,               /* CE: an overcharge shot overheats the pistol */
            .projectile = HALO_PROJ_PLASMA_PISTOL_BOLT,
            .charged_projectile = HALO_PROJ_PLASMA_PISTOL_CHARGED,
        },
        .fp_offset = { 0.10f, -0.05f, -0.06f },
    },
};

const HaloGrenadeDef g_halo_grenades[HALO_GRENADE_COUNT] = {
    [HALO_GRENADE_PLASMA] = {
        .name = "plasma grenade",
        .projectile = HALO_PROJ_PLASMA_GRENADE,
        .throw_velocity = 6.0f,             /* [approx] ~10 wu range on flat ground */
        .throw_pitch_bias = D2R(12.0f),     /* [approx] */
        .throw_delay = 0.25f,
        .maximum_count = 4,
    },
};

const HaloBipedDef g_halo_bipeds[HALO_BIPED_COUNT] = {
    [HALO_BIPED_CYBORG] = {
        .name = "cyborg",
        /* [engine] biped_update_moving.c player physics constants */
        .run_forward_speed = 2.25f, .run_backward_speed = 2.0f, .run_sideways_speed = 2.0f,
        .run_acceleration = 0.32f * HALO_TICKS_PER_SECOND,
        .crouch_speed_scale = 0.5f,          /* [approx] */
        .airborne_acceleration = 3.0f,       /* [approx] light air control */
        .jump_velocity = 1.4f,               /* [approx] ~0.3 wu apex */
        .standing_camera_height = 0.62f, .crouching_camera_height = 0.35f, /* [approx] */
        .standing_collision_height = 0.70f, .crouching_collision_height = 0.45f,
        .collision_radius = 0.2f,
        .head_height_fraction = 0.85f,
        .maximum_body_vitality = 75.0f, .maximum_shield_vitality = 75.0f, /* [approx] CE cyborg */
        .shield_stun_time = 2.0f, .shield_recharge_time = 2.0f,         /* [approx] */
        .head_damage_multiplier = 1.0f,
        .shield_damage_multiplier = SHIELD_MULT_STANDARD,
        .body_damage_multiplier = BODY_MULT_STANDARD,
    },
    [HALO_BIPED_GRUNT] = {
        .name = "grunt",
        .run_forward_speed = 1.45f, .run_backward_speed = 1.2f, .run_sideways_speed = 1.2f, /* [approx] */
        .run_acceleration = 0.32f * HALO_TICKS_PER_SECOND,
        .crouch_speed_scale = 0.5f,
        .airborne_acceleration = 1.5f,
        .jump_velocity = 1.0f,
        .standing_camera_height = 0.38f, .crouching_camera_height = 0.28f,
        .standing_collision_height = 0.46f, .crouching_collision_height = 0.34f,
        .collision_radius = 0.16f,
        .head_height_fraction = 0.72f,
        .maximum_body_vitality = 22.0f,      /* [approx] 3-4 AR rounds on Normal */
        .maximum_shield_vitality = 0.0f,
        .head_damage_multiplier = 3.0f,
        .shield_damage_multiplier = SHIELD_MULT_STANDARD,
        .body_damage_multiplier = BODY_MULT_UNSHIELDED,
    },
    [HALO_BIPED_ELITE] = {
        .name = "elite",
        .run_forward_speed = 2.3f, .run_backward_speed = 2.0f, .run_sideways_speed = 2.2f, /* [approx] */
        .run_acceleration = 0.4f * HALO_TICKS_PER_SECOND,
        .crouch_speed_scale = 0.5f,
        .airborne_acceleration = 2.0f,
        .jump_velocity = 1.6f,
        .standing_camera_height = 0.68f, .crouching_camera_height = 0.45f,
        .standing_collision_height = 0.76f, .crouching_collision_height = 0.52f,
        .collision_radius = 0.22f,
        .head_height_fraction = 0.84f,
        .maximum_body_vitality = 45.0f, .maximum_shield_vitality = 45.0f, /* [approx] minor elite */
        .shield_stun_time = 2.5f, .shield_recharge_time = 2.0f,
        .head_damage_multiplier = 2.0f,
        .shield_damage_multiplier = SHIELD_MULT_STANDARD,
        .body_damage_multiplier = BODY_MULT_STANDARD,
    },
};

/* All [approx]: CE actor tags are not present in the reference repos. */
const HaloActorDef g_halo_actors[HALO_ACTOR_TYPE_COUNT] = {
    [HALO_ACTOR_GRUNT] = {
        .name = "grunt",
        .biped = HALO_BIPED_GRUNT,
        .weapon = HALO_WEAPON_PLASMA_PISTOL,
        .plasma_grenades = 1,
        .vision_range = 10.0f, .vision_half_angle = D2R(60.0f),
        .hearing_range = 14.0f,
        .acknowledge_time = 0.6f,
        .combat_range_min = 2.5f, .combat_range_max = 6.5f,
        .burst_duration_min = 0.6f, .burst_duration_max = 1.3f,
        .burst_separation_min = 0.8f, .burst_separation_max = 1.8f,
        .aim_error_angle = D2R(2.5f),
        .semiauto_fire_rate = 3.0f,
        .movement_speed_scale = 1.0f,
        .strafe_chance = 0.35f,
        .panic_chance_leader_killed = 0.8f,
        .panic_chance_friend_killed = 0.25f,
        .flee_body_fraction = 0.35f,
        .flee_time_min = 3.0f, .flee_time_max = 5.0f,
        .grenade_chance = 0.15f,      /* per second, once off cooldown */
    },
    [HALO_ACTOR_ELITE] = {
        .name = "elite",
        .biped = HALO_BIPED_ELITE,
        .weapon = HALO_WEAPON_PLASMA_PISTOL,
        .plasma_grenades = 2,
        .vision_range = 14.0f, .vision_half_angle = D2R(70.0f),
        .hearing_range = 18.0f,
        .acknowledge_time = 0.3f,
        .combat_range_min = 2.5f, .combat_range_max = 7.0f,
        .burst_duration_min = 0.8f, .burst_duration_max = 1.6f,
        .burst_separation_min = 0.5f, .burst_separation_max = 1.2f,
        .aim_error_angle = D2R(1.5f),
        .semiauto_fire_rate = 4.0f,
        .movement_speed_scale = 1.0f,
        .strafe_chance = 0.9f,
        .flee_body_fraction = 0.0f,
        .grenade_chance = 0.25f,
        .has_melee = 1, .melee_range = 0.55f, .melee_damage = 40.0f,
        .is_leader = 1,
    },
};
