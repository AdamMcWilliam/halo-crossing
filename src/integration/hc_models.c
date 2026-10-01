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

static const HcBox k_fp_pp[] = {
    { 0.170f, -0.060f, -0.080f, 0.075f, 0.035f, 0.035f, 0x8E9B86FF, HC_PART_WEAPON },
    { 0.250f, -0.060f, -0.070f, 0.020f, 0.030f, 0.012f, 0x7A8573FF, HC_PART_WEAPON },
    { 0.180f, -0.060f, -0.040f, 0.050f, 0.020f, 0.010f, 0x6E7A68FF, HC_PART_WEAPON },
    { 0.262f, -0.060f, -0.070f, 0.006f, 0.016f, 0.012f, 0x8CFF6AFF, HC_PART_GLOW },
    { 0.130f, -0.065f, -0.112f, 0.035f, 0.030f, 0.030f, 0x2A2A2AFF, HC_PART_ARM_R },
    { 0.050f, -0.080f, -0.135f, 0.070f, 0.038f, 0.038f, 0x5B6B3EFF, HC_PART_ARM_R },
};

static const HcModel k_models[HALO_BIPED_COUNT] = {
    [HALO_BIPED_CYBORG] = { k_cyborg, N(k_cyborg), 0.34f, 0.57f },
    [HALO_BIPED_GRUNT] = { k_grunt, N(k_grunt), 0.15f, 0.27f },
    [HALO_BIPED_ELITE] = { k_elite, N(k_elite), 0.37f, 0.60f },
};

static const HcModel k_fp_models[HALO_WEAPON_COUNT] = {
    [HALO_WEAPON_ASSAULT_RIFLE] = { k_fp_ar, N(k_fp_ar), 0, 0 },
    [HALO_WEAPON_PLASMA_PISTOL] = { k_fp_pp, N(k_fp_pp), 0, 0 },
};

const HcModel* hc_model_for_biped(HaloBipedId biped) {
    return (biped >= 0 && biped < HALO_BIPED_COUNT) ? &k_models[biped] : &k_models[HALO_BIPED_GRUNT];
}

const HcModel* hc_model_first_person(HaloWeaponId weapon) {
    return (weapon >= 0 && weapon < HALO_WEAPON_COUNT) ? &k_fp_models[weapon] : NULL;
}
