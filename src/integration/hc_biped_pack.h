/* Slot layout of the third-person pack (HCBP, see hc_fp_pack.h). Must match
 * CHARACTERS/WEAPONS and ANIM_SLOTS in tools/halo_import/biped_pack.py. */
#ifndef HC_BIPED_PACK_H
#define HC_BIPED_PACK_H

#define HC_BP_PACK_NAME "bipeds.hcpk"

typedef enum HcBpModel {
    HC_BP_GRUNT,
    HC_BP_ELITE,
    HC_BP_PLASMA_PISTOL,
    HC_BP_PLASMA_RIFLE,
    HC_BP_NEEDLER,
    HC_BP_FUEL_ROD,
    HC_BP_MODEL_COUNT
} HcBpModel;

typedef enum HcBpAnim {
    HC_BP_IDLE,
    HC_BP_MOVE_FRONT,
    HC_BP_MOVE_BACK,
    HC_BP_MOVE_LEFT,
    HC_BP_MOVE_RIGHT,
    HC_BP_CROUCH_IDLE,
    HC_BP_CROUCH_MOVE,
    HC_BP_FLEE,
    HC_BP_AIRBORNE,
    HC_BP_LAND,
    HC_BP_THROW_GRENADE,
    HC_BP_MELEE,
    HC_BP_SURPRISE,
    HC_BP_WARN,
    HC_BP_EVADE_LEFT,
    HC_BP_EVADE_RIGHT,
    HC_BP_PING_FRONT,
    HC_BP_PING_BACK,
    HC_BP_DIE_FRONT,        /* shot from the front */
    HC_BP_DIE_BACK,
    HC_BP_DIE_LEFT,
    HC_BP_DIE_RIGHT,
    HC_BP_DIE_HARD_FRONT,   /* thrown by an explosion in front */
    HC_BP_DIE_HARD_BACK,
    HC_BP_FIRE_PISTOL,      /* overlays */
    HC_BP_FIRE_RIFLE,
    HC_BP_FIRE_NEEDLER,
    HC_BP_HEAVY_IDLE,       /* two-handed: the fuel rod */
    HC_BP_HEAVY_MOVE_FRONT,
    HC_BP_HEAVY_MOVE_BACK,
    HC_BP_HEAVY_MOVE_LEFT,
    HC_BP_HEAVY_MOVE_RIGHT,
    HC_BP_ANIM_COUNT
} HcBpAnim;

#endif
