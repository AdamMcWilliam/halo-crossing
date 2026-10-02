"""First-person weapon pack (HCFP): the Chief's arms + each weapon, skinned,
with animations. Layout: see skin_pack.py.

Positions are Halo first-person camera space in world units: x forward, y left, z up.
"""
from . import anims, models, skin_pack

MAGIC = b"HCFP"

# Order matches HaloWeaponId (src/halo/halo_defs.h).
WEAPONS = [
    "assault rifle", "pistol", "shotgun", "sniper rifle", "rocket launcher",
    "flamethrower", "plasma pistol", "plasma rifle", "needler", None,  # no fp fuel rod on Xbox
]

# Order matches HcFpAnim (src/integration/hc_fp_pack.h). First name found wins.
ANIM_SLOTS = [
    ("idle",),
    ("fire-1", "firing"),
    ("ready",),
    ("reload-full", "reload-empty"),
    ("reload-empty", "reload-full"),
    ("throw-grenade",),
    ("overheated",),
    ("posing",),
    ("enter",),        # shotgun: open the loading port
    ("exit-full",),    # shotgun: back to idle
    ("fire-2",),       # plasma pistol charged shot
    ("overheating",),
    ("ammunition",),   # overlay posed by rounds spent: the needler's needles sink as it empties
]

ARMS = r"characters\cyborg\fp\fp"
MUZZLE_MARKERS = ("primary trigger", "primary_trigger")


def _fp_path(weapon):
    return "weapons\\%s\\fp\\fp" % weapon


def build_weapon(m, weapon, arms, tex, log):
    gun_tag = m.find("mode", _fp_path(weapon))
    antr_tag = m.find("antr", _fp_path(weapon))
    if gun_tag is None or antr_tag is None:
        log("  %s: no first-person model in this map" % weapon)
        return None
    gun = models.read_model(m, gun_tag)
    graph = anims.read_anim_graph(m, antr_tag)
    md = skin_pack.ModelData(weapon, [n.name for n in graph.nodes],
                             [n.parent if n.parent != i else -1 for i, n in enumerate(graph.nodes)])
    for model in (arms, gun):
        md.add_geometry(m, model, [skin_pack.shader_texture(m, tex, s) for s in model.shaders], log)
    md.end_lod()

    for names in ANIM_SLOTS:
        md.slots.append(next((graph.get("first-person " + n) for n in names if graph.get("first-person " + n)), None))

    marks = models.read_markers(m, gun_tag)
    for name in MUZZLE_MARKERS:
        if marks.get(name):
            node, t = marks[name][0]
            if 0 <= node < len(gun.nodes) and md.node_index(gun.nodes[node].name) >= 0:
                md.muzzle = (md.node_index(gun.nodes[node].name), t)
            break
    log("  %-16s %s  muzzle %s" % (weapon, md.summary(), md.muzzle[0]))
    return md


def build_pack(m, log=print):
    arms_tag = m.find("mode", ARMS)
    if arms_tag is None:
        raise ValueError("map has no first-person arms (%s)" % ARMS)
    arms = models.read_model(m, arms_tag)
    tex = skin_pack.Textures(m)
    weapons = [build_weapon(m, name, arms, tex, log) if name else None for name in WEAPONS]
    return skin_pack.write_pack(MAGIC, weapons, tex, len(ANIM_SLOTS), log), weapons
