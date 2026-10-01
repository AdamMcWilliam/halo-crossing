/* Placeholder box art for Halo characters and weapons (no retail assets).
 * Coordinates are Halo world units in the model's frame: f = forward,
 * l = left, u = up, origin at the feet (characters) or the eye (first-person
 * weapons). Replaced by imported meshes once AssetAdapter exists. */
#ifndef HC_MODELS_H
#define HC_MODELS_H

#include "halo/halo_defs.h"
#include "types.h"

typedef enum HcPart {
    HC_PART_BODY,
    HC_PART_LEG_L,
    HC_PART_LEG_R,
    HC_PART_ARM_L,
    HC_PART_ARM_R,
    HC_PART_HEAD,
    HC_PART_WEAPON,
    HC_PART_GLOW,      /* tinted at draw time (emitters, ammo counters) */
} HcPart;

typedef struct HcBox {
    float f, l, u;
    float hf, hl, hu;
    u32 rgba;
    u8 part;
} HcBox;

typedef struct HcModel {
    const HcBox* boxes;
    int count;
    float hip_u;       /* leg swing pivot height */
    float shoulder_u;  /* arm pivot height */
} HcModel;

const HcModel* hc_model_for_biped(HaloBipedId biped);
const HcModel* hc_model_first_person(HaloWeaponId weapon);

#endif
