#include "hc_models.h"

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Grunt (minor): orange armor, methane tank, breather mask. ~0.46 wu tall. */
static const HcBox k_grunt[] = {
    /*  f       l       u      hf     hl     hu     color        part */
    { 0.00f, 0.055f, 0.075f, 0.040f, 0.035f, 0.075f, 0x4B4A3CFF, HC_PART_LEG_L },
    { 0.00f, -0.055f, 0.075f, 0.040f, 0.035f, 0.075f, 0x4B4A3CFF, HC_PART_LEG_R },
    { 0.00f, 0.000f, 0.215f, 0.075f, 0.110f, 0.075f, 0xD9792BFF, HC_PART_BODY },
    { 0.07f, 0.000f, 0.170f, 0.012f, 0.075f, 0.045f, 0x7D7566FF, HC_PART_BODY },
    { -0.10f, 0.000f, 0.270f, 0.050f, 0.085f, 0.110f, 0x76BFDDFF, HC_PART_BODY },
    { -0.10f, 0.000f, 0.392f, 0.030f, 0.050f, 0.014f, 0x8C8C8CFF, HC_PART_BODY },
    { 0.03f, 0.000f, 0.340f, 0.060f, 0.068f, 0.055f, 0x857C6EFF, HC_PART_HEAD },
    { 0.095f, 0.000f, 0.322f, 0.020f, 0.050f, 0.030f, 0x6E88A6FF, HC_PART_HEAD },
    { 0.088f, 0.035f, 0.372f, 0.008f, 0.012f, 0.010f, 0xFF9F2EFF, HC_PART_HEAD },
    { 0.088f, -0.035f, 0.372f, 0.008f, 0.012f, 0.010f, 0xFF9F2EFF, HC_PART_HEAD },
    { 0.03f, 0.135f, 0.200f, 0.028f, 0.028f, 0.075f, 0x857C6EFF, HC_PART_ARM_L },
    { 0.03f, -0.135f, 0.200f, 0.028f, 0.028f, 0.075f, 0x857C6EFF, HC_PART_ARM_R },
    { 0.15f, -0.060f, 0.185f, 0.060f, 0.025f, 0.028f, 0x9AA48CFF, HC_PART_WEAPON },
    { 0.21f, -0.060f, 0.190f, 0.008f, 0.020f, 0.020f, 0x8CFF6AFF, HC_PART_GLOW },
};

/* Elite (minor): blue armor, digitigrade legs, mandibles. ~0.76 wu tall. */
static const HcBox k_elite[] = {
    { 0.00f, 0.075f, 0.270f, 0.050f, 0.045f, 0.100f, 0x2F3A9EFF, HC_PART_LEG_L },
    { 0.00f, -0.075f, 0.270f, 0.050f, 0.045f, 0.100f, 0x2F3A9EFF, HC_PART_LEG_R },
    { -0.03f, 0.075f, 0.090f, 0.040f, 0.040f, 0.090f, 0x3B3B46FF, HC_PART_LEG_L },
    { -0.03f, -0.075f, 0.090f, 0.040f, 0.040f, 0.090f, 0x3B3B46FF, HC_PART_LEG_R },
    { 0.00f, 0.000f, 0.500f, 0.090f, 0.140f, 0.120f, 0x3549C8FF, HC_PART_BODY },
    { -0.08f, 0.000f, 0.560f, 0.040f, 0.120f, 0.090f, 0x2A3AA0FF, HC_PART_BODY },
    { 0.06f, 0.000f, 0.700f, 0.070f, 0.055f, 0.050f, 0x4A5AD8FF, HC_PART_HEAD },
    { 0.12f, 0.000f, 0.655f, 0.030f, 0.045f, 0.030f, 0x6A6458FF, HC_PART_HEAD },
    { 0.04f, 0.170f, 0.480f, 0.035f, 0.035f, 0.120f, 0x3549C8FF, HC_PART_ARM_L },
    { 0.04f, -0.170f, 0.480f, 0.035f, 0.035f, 0.120f, 0x3549C8FF, HC_PART_ARM_R },
    { 0.20f, -0.080f, 0.450f, 0.060f, 0.025f, 0.028f, 0x9AA48CFF, HC_PART_WEAPON },
    { 0.26f, -0.080f, 0.455f, 0.008f, 0.020f, 0.020f, 0x8CFF6AFF, HC_PART_GLOW },
};

/* Master Chief (third person): Mjolnir green, gold visor. ~0.70 wu tall. */
static const HcBox k_cyborg[] = {
    { 0.00f, 0.060f, 0.170f, 0.050f, 0.050f, 0.170f, 0x55633AFF, HC_PART_LEG_L },
    { 0.00f, -0.060f, 0.170f, 0.050f, 0.050f, 0.170f, 0x55633AFF, HC_PART_LEG_R },
    { 0.00f, 0.000f, 0.470f, 0.080f, 0.130f, 0.130f, 0x5B6B3EFF, HC_PART_BODY },
    { -0.02f, 0.000f, 0.330f, 0.060f, 0.100f, 0.030f, 0x2E2E2EFF, HC_PART_BODY },
    { 0.01f, 0.000f, 0.645f, 0.065f, 0.060f, 0.055f, 0x5B6B3EFF, HC_PART_HEAD },
    { 0.068f, 0.000f, 0.648f, 0.010f, 0.045f, 0.022f, 0xE8B33AFF, HC_PART_HEAD },
    { 0.03f, 0.165f, 0.450f, 0.040f, 0.040f, 0.120f, 0x55633AFF, HC_PART_ARM_L },
    { 0.03f, -0.165f, 0.450f, 0.040f, 0.040f, 0.120f, 0x55633AFF, HC_PART_ARM_R },
    { 0.18f, -0.090f, 0.400f, 0.130f, 0.022f, 0.030f, 0x3E4636FF, HC_PART_WEAPON },
    { 0.12f, -0.090f, 0.433f, 0.012f, 0.012f, 0.006f, 0x55D6FFFF, HC_PART_GLOW },
};

/* First-person MA5B: positioned below and right of the eye. */
static const HcBox k_fp_ar[] = {
    { 0.200f, -0.060f, -0.075f, 0.130f, 0.022f, 0.030f, 0x3E4636FF, HC_PART_WEAPON },
    { 0.360f, -0.060f, -0.068f, 0.040f, 0.016f, 0.018f, 0x2B2E2AFF, HC_PART_WEAPON },
    { 0.170f, -0.060f, -0.125f, 0.025f, 0.017f, 0.035f, 0x2B2E2AFF, HC_PART_WEAPON },
    { 0.200f, -0.060f, -0.040f, 0.070f, 0.008f, 0.009f, 0x30352BFF, HC_PART_WEAPON },
    { 0.040f, -0.060f, -0.085f, 0.050f, 0.020f, 0.030f, 0x3E4636FF, HC_PART_WEAPON },
    { 0.120f, -0.060f, -0.042f, 0.014f, 0.013f, 0.006f, 0x55D6FFFF, HC_PART_GLOW },
    { 0.090f, -0.072f, -0.110f, 0.035f, 0.030f, 0.030f, 0x2A2A2AFF, HC_PART_ARM_R },
    { 0.010f, -0.085f, -0.135f, 0.070f, 0.038f, 0.038f, 0x5B6B3EFF, HC_PART_ARM_R },
    { 0.290f, -0.040f, -0.103f, 0.030f, 0.028f, 0.025f, 0x2A2A2AFF, HC_PART_ARM_L },
    { 0.200f, 0.010f, -0.140f, 0.090f, 0.038f, 0.038f, 0x5B6B3EFF, HC_PART_ARM_L },
};

/* Chief's gloved hand + green forearm; one-handed weapons only use the right. */
#define HAND_R(f) { (f), -0.065f, -0.112f, 0.035f, 0.030f, 0.030f, 0x2A2A2AFF, HC_PART_ARM_R }, \
                  { (f) - 0.080f, -0.080f, -0.135f, 0.070f, 0.038f, 0.038f, 0x5B6B3EFF, HC_PART_ARM_R }
#define HAND_L(f) { (f), -0.040f, -0.103f, 0.030f, 0.028f, 0.025f, 0x2A2A2AFF, HC_PART_ARM_L }, \
                  { (f) - 0.090f, 0.010f, -0.140f, 0.090f, 0.038f, 0.038f, 0x5B6B3EFF, HC_PART_ARM_L }

static const HcBox k_fp_pp[] = {
    { 0.170f, -0.060f, -0.080f, 0.075f, 0.035f, 0.035f, 0x8E9B86FF, HC_PART_WEAPON },
    { 0.250f, -0.060f, -0.070f, 0.020f, 0.030f, 0.012f, 0x7A8573FF, HC_PART_WEAPON },
    { 0.180f, -0.060f, -0.040f, 0.050f, 0.020f, 0.010f, 0x6E7A68FF, HC_PART_WEAPON },
    { 0.262f, -0.060f, -0.070f, 0.006f, 0.016f, 0.012f, 0x8CFF6AFF, HC_PART_GLOW },
    HAND_R(0.130f),
};

/* M6D: stubby slide with the smart-link scope on top. */
static const HcBox k_fp_pistol[] = {
    { 0.180f, -0.060f, -0.070f, 0.075f, 0.018f, 0.017f, 0x4A4E52FF, HC_PART_WEAPON },
    { 0.150f, -0.060f, -0.092f, 0.050f, 0.016f, 0.012f, 0x34373AFF, HC_PART_WEAPON },
    { 0.125f, -0.060f, -0.122f, 0.017f, 0.014f, 0.032f, 0x26282AFF, HC_PART_WEAPON },
    { 0.170f, -0.060f, -0.046f, 0.032f, 0.010f, 0.009f, 0x2A2C2EFF, HC_PART_WEAPON },
    { 0.137f, -0.060f, -0.046f, 0.003f, 0.007f, 0.006f, 0xFF4A3AFF, HC_PART_GLOW },
    HAND_R(0.125f),
};

static const HcBox k_fp_shotgun[] = {
    { 0.300f, -0.060f, -0.064f, 0.170f, 0.012f, 0.012f, 0x2E2E30FF, HC_PART_WEAPON },
    { 0.260f, -0.060f, -0.088f, 0.130f, 0.011f, 0.011f, 0x3A3A3CFF, HC_PART_WEAPON },
    { 0.100f, -0.060f, -0.078f, 0.080f, 0.022f, 0.030f, 0x4A4D50FF, HC_PART_WEAPON },
    { 0.270f, -0.060f, -0.088f, 0.045f, 0.019f, 0.019f, 0x5A4632FF, HC_PART_WEAPON },
    { -0.010f, -0.060f, -0.098f, 0.050f, 0.018f, 0.030f, 0x3A3A3CFF, HC_PART_WEAPON },
    { 0.080f, -0.060f, -0.044f, 0.010f, 0.010f, 0.006f, 0xFFB050FF, HC_PART_GLOW },
    HAND_R(0.090f),
    HAND_L(0.270f),
};

static const HcBox k_fp_sniper[] = {
    { 0.180f, -0.060f, -0.080f, 0.160f, 0.020f, 0.026f, 0x50555AFF, HC_PART_WEAPON },
    { 0.440f, -0.060f, -0.072f, 0.130f, 0.009f, 0.009f, 0x2E3033FF, HC_PART_WEAPON },
    { 0.150f, -0.060f, -0.034f, 0.085f, 0.016f, 0.016f, 0x22252AFF, HC_PART_WEAPON },
    { 0.120f, -0.060f, -0.120f, 0.020f, 0.014f, 0.030f, 0x3A3D40FF, HC_PART_WEAPON },
    { -0.030f, -0.060f, -0.095f, 0.060f, 0.020f, 0.030f, 0x45494DFF, HC_PART_WEAPON },
    { 0.237f, -0.060f, -0.034f, 0.004f, 0.012f, 0.012f, 0x7EC8FFFF, HC_PART_GLOW },
    HAND_R(0.080f),
    HAND_L(0.300f),
};

/* M19: a fat olive tube carried on the right shoulder. */
static const HcBox k_fp_rocket[] = {
    { 0.150f, -0.075f, -0.055f, 0.250f, 0.050f, 0.050f, 0x4F5A3AFF, HC_PART_WEAPON },
    { 0.405f, -0.075f, -0.055f, 0.012f, 0.056f, 0.056f, 0x2B2E2AFF, HC_PART_WEAPON },
    { 0.100f, -0.075f, -0.125f, 0.020f, 0.015f, 0.035f, 0x2B2E2AFF, HC_PART_WEAPON },
    { 0.130f, -0.030f, 0.000f, 0.030f, 0.008f, 0.012f, 0x2B2E2AFF, HC_PART_WEAPON },
    { 0.060f, -0.024f, -0.040f, 0.012f, 0.004f, 0.012f, 0xFF5050FF, HC_PART_GLOW },
    HAND_R(0.100f),
    HAND_L(0.250f),
};

static const HcBox k_fp_flamer[] = {
    { 0.170f, -0.065f, -0.085f, 0.130f, 0.030f, 0.035f, 0x6A5A3AFF, HC_PART_WEAPON },
    { 0.110f, -0.065f, -0.135f, 0.060f, 0.035f, 0.030f, 0xA03A2AFF, HC_PART_WEAPON },
    { 0.330f, -0.065f, -0.080f, 0.045f, 0.014f, 0.014f, 0x3A3A3AFF, HC_PART_WEAPON },
    { 0.380f, -0.065f, -0.080f, 0.008f, 0.010f, 0.010f, 0x60A8FFFF, HC_PART_GLOW },
    HAND_R(0.090f),
    HAND_L(0.270f),
};

static const HcBox k_fp_prifle[] = {
    { 0.170f, -0.060f, -0.082f, 0.100f, 0.040f, 0.026f, 0x6670A8FF, HC_PART_WEAPON },
    { 0.275f, -0.060f, -0.060f, 0.045f, 0.012f, 0.008f, 0x58609AFF, HC_PART_WEAPON },
    { 0.275f, -0.060f, -0.104f, 0.045f, 0.012f, 0.008f, 0x58609AFF, HC_PART_WEAPON },
    { 0.140f, -0.060f, -0.046f, 0.040f, 0.012f, 0.012f, 0x4A5288FF, HC_PART_WEAPON },
    { 0.258f, -0.060f, -0.082f, 0.008f, 0.022f, 0.012f, 0x6AC8FFFF, HC_PART_GLOW },
    HAND_R(0.140f),
};

/* Needler: pink crystals ride on top and are spent as it fires. */
static const HcBox k_fp_needler[] = {
    { 0.160f, -0.060f, -0.086f, 0.090f, 0.030f, 0.030f, 0x5A3A7AFF, HC_PART_WEAPON },
    { 0.245f, -0.060f, -0.076f, 0.030f, 0.020f, 0.022f, 0x6A4A8AFF, HC_PART_WEAPON },
    { 0.120f, -0.060f, -0.044f, 0.008f, 0.007f, 0.020f, 0xFF5AD2FF, HC_PART_GLOW },
    { 0.150f, -0.060f, -0.044f, 0.008f, 0.007f, 0.020f, 0xFF5AD2FF, HC_PART_GLOW },
    { 0.180f, -0.060f, -0.044f, 0.008f, 0.007f, 0.020f, 0xFF5AD2FF, HC_PART_GLOW },
    { 0.210f, -0.060f, -0.044f, 0.008f, 0.007f, 0.020f, 0xFF5AD2FF, HC_PART_GLOW },
    HAND_R(0.130f),
};

static const HcBox k_fp_fuelrod[] = {
    { 0.170f, -0.075f, -0.070f, 0.200f, 0.045f, 0.045f, 0x4E6A5AFF, HC_PART_WEAPON },
    { 0.385f, -0.075f, -0.045f, 0.030f, 0.012f, 0.012f, 0x3E5448FF, HC_PART_WEAPON },
    { 0.385f, -0.075f, -0.095f, 0.030f, 0.012f, 0.012f, 0x3E5448FF, HC_PART_WEAPON },
    { 0.100f, -0.075f, -0.130f, 0.020f, 0.015f, 0.035f, 0x2F3F36FF, HC_PART_WEAPON },
    { 0.150f, -0.028f, -0.070f, 0.070f, 0.004f, 0.014f, 0x7CFF3CFF, HC_PART_GLOW },
    HAND_R(0.100f),
    HAND_L(0.260f),
};

static const HcModel k_models[HALO_BIPED_COUNT] = {
    [HALO_BIPED_CYBORG] = { k_cyborg, N(k_cyborg), 0.34f, 0.57f },
    [HALO_BIPED_GRUNT] = { k_grunt, N(k_grunt), 0.15f, 0.27f },
    [HALO_BIPED_ELITE] = { k_elite, N(k_elite), 0.37f, 0.60f },
    [HALO_BIPED_VILLAGER] = { 0, 0, 0, 0 }, /* Animal Crossing draws the real villager */
};

static const HcModel k_fp_models[HALO_WEAPON_COUNT] = {
    [HALO_WEAPON_ASSAULT_RIFLE] = { k_fp_ar, N(k_fp_ar), 0, 0 },
    [HALO_WEAPON_PISTOL] = { k_fp_pistol, N(k_fp_pistol), 0, 0 },
    [HALO_WEAPON_SHOTGUN] = { k_fp_shotgun, N(k_fp_shotgun), 0, 0 },
    [HALO_WEAPON_SNIPER_RIFLE] = { k_fp_sniper, N(k_fp_sniper), 0, 0 },
    [HALO_WEAPON_ROCKET_LAUNCHER] = { k_fp_rocket, N(k_fp_rocket), 0, 0 },
    [HALO_WEAPON_FLAMETHROWER] = { k_fp_flamer, N(k_fp_flamer), 0, 0 },
    [HALO_WEAPON_PLASMA_PISTOL] = { k_fp_pp, N(k_fp_pp), 0, 0 },
    [HALO_WEAPON_PLASMA_RIFLE] = { k_fp_prifle, N(k_fp_prifle), 0, 0 },
    [HALO_WEAPON_NEEDLER] = { k_fp_needler, N(k_fp_needler), 0, 0 },
    [HALO_WEAPON_FUEL_ROD] = { k_fp_fuelrod, N(k_fp_fuelrod), 0, 0 },
};

/* Third-person: one weapon box per biped, recolored to whatever is held. */
static const u32 k_weapon_body[HALO_WEAPON_COUNT] = {
    [HALO_WEAPON_ASSAULT_RIFLE] = 0x3E4636FF, [HALO_WEAPON_PISTOL] = 0x4A4E52FF,
    [HALO_WEAPON_SHOTGUN] = 0x2E2E30FF,       [HALO_WEAPON_SNIPER_RIFLE] = 0x50555AFF,
    [HALO_WEAPON_ROCKET_LAUNCHER] = 0x4F5A3AFF, [HALO_WEAPON_FLAMETHROWER] = 0x6A5A3AFF,
    [HALO_WEAPON_PLASMA_PISTOL] = 0x9AA48CFF, [HALO_WEAPON_PLASMA_RIFLE] = 0x6670A8FF,
    [HALO_WEAPON_NEEDLER] = 0x5A3A7AFF,       [HALO_WEAPON_FUEL_ROD] = 0x4E6A5AFF,
};

u32 hc_weapon_body_rgba(HaloWeaponId weapon, u32 fallback) {
    return (weapon >= 0 && weapon < HALO_WEAPON_COUNT) ? k_weapon_body[weapon] : fallback;
}

u32 hc_weapon_glow_rgba(HaloWeaponId weapon, u32 fallback) {
    if (weapon < 0 || weapon >= HALO_WEAPON_COUNT) return fallback;
    return g_halo_projectiles[g_halo_weapons[weapon].trigger.projectile].render_rgba;
}

const HcModel* hc_model_for_biped(HaloBipedId biped) {
    return (biped >= 0 && biped < HALO_BIPED_COUNT) ? &k_models[biped] : &k_models[HALO_BIPED_GRUNT];
}

const HcModel* hc_model_first_person(HaloWeaponId weapon) {
    return (weapon >= 0 && weapon < HALO_WEAPON_COUNT) ? &k_fp_models[weapon] : NULL;
}
