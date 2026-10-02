"""First-person weapon pack (HCFP): the Chief's arms + each weapon, skinned,
with animations. Layout: see skin_pack.py.

Positions are Halo first-person camera space in world units: x forward, y left, z up.
"""
from . import anims, models, skin_pack

MAGIC = b"HCFP"

# Order matches HaloWeaponId (src/halo/halo_defs.h).
WEAPONS = [
    "assault rifle", "pistol", "shotgun", "sniper rifle", "rocket launcher",
    "flamethrower", "plasma pistol", "plasma rifle", "needler", None,
]
# The Xbox discs have no first-person fuel rod: carry the Grunts' third-person
# gun on the rocket launcher's first-person rig (both ride the right shoulder).
FUEL_ROD_SLOT = 9
FUEL_ROD = ("c40", r"weapons\fuel rod gun\fuel rod gun", "rocket launcher")

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


def _assemble(m, name, antr_tag, arms, gun_map, gun_tag, tex, log):
    """Arms plus a gun model (possibly from another map) on a first-person rig from `m`."""
    gun = models.read_model(gun_map, gun_tag)
    graph = anims.read_anim_graph(m, antr_tag)
    md = skin_pack.ModelData(name, [n.name for n in graph.nodes],
                             [n.parent if n.parent != i else -1 for i, n in enumerate(graph.nodes)])
    md.add_geometry(m, arms, [skin_pack.shader_texture(m, tex, s) for s in arms.shaders], log)
    tex.m = gun_map
    md.add_geometry(gun_map, gun, [skin_pack.shader_texture(gun_map, tex, s) for s in gun.shaders], log)
    tex.m = m
    md.end_lod()

    for names in ANIM_SLOTS:
        md.slots.append(next((graph.get("first-person " + n) for n in names if graph.get("first-person " + n)), None))

    marks = models.read_markers(gun_map, gun_tag)
    for marker in MUZZLE_MARKERS:
        if marks.get(marker):
            node, t = marks[marker][0]
            if 0 <= node < len(gun.nodes) and md.node_index(gun.nodes[node].name) >= 0:
                md.muzzle = (md.node_index(gun.nodes[node].name), t)
            break
    log("  %-16s %s  muzzle %s" % (name, md.summary(), md.muzzle[0]))
    return md


def build_weapon(m, weapon, arms, tex, log):
    gun_tag = m.find("mode", _fp_path(weapon))
    antr_tag = m.find("antr", _fp_path(weapon))
    if gun_tag is None or antr_tag is None:
        log("  %s: no first-person model in this map" % weapon)
        return None
    return _assemble(m, weapon, antr_tag, arms, m, gun_tag, tex, log)


def build_fuel_rod(m, load_map, arms, tex, log):
    map_name, path, rig = FUEL_ROD
    gun_map = load_map(map_name)
    gun_tag = gun_map.find("mode", path)
    antr_tag = m.find("antr", _fp_path(rig))
    if gun_tag is None or antr_tag is None:
        log("  fuel rod: no third-person gun in %s or no %s rig" % (map_name, rig))
        return None
    return _assemble(m, "fuel rod", antr_tag, arms, gun_map, gun_tag, tex, log)


def build_pack(m, load_map=None, log=print):
    """load_map(name) -> CacheMap, for the fuel rod's gun; without it the fuel rod is left out."""
    arms_tag = m.find("mode", ARMS)
    if arms_tag is None:
        raise ValueError("map has no first-person arms (%s)" % ARMS)
    arms = models.read_model(m, arms_tag)
    tex = skin_pack.Textures(m)
    weapons = [build_weapon(m, name, arms, tex, log) if name else None for name in WEAPONS]
    if load_map is not None:
        weapons[FUEL_ROD_SLOT] = build_fuel_rod(m, load_map, arms, tex, log)
    return skin_pack.write_pack(MAGIC, weapons, tex, len(ANIM_SLOTS), log), weapons
