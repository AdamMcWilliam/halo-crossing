"""Third-person pack (HCBP): Covenant bipeds with their animations, and the
weapons they hold. Layout: see skin_pack.py.

Positions are Halo model space in world units: x forward, y left, z up, feet
at the origin. Weapons are rigid (one node) with their grip at the origin.
"""
import numpy as np

from . import anims, models, skin_pack

MAGIC = b"HCBP"
CHARACTER_MAP = "b30"   # The Silent Cartographer: Grunts, Elites, every Covenant hand weapon
FUEL_ROD_MAP = "c40"

# Order matches HcBpModel (src/integration/hc_biped_view.h).
# (name, map, model/animation tag, hand marker, actor variant whose colour change is used)
CHARACTERS = [
    ("grunt", CHARACTER_MAP, r"characters\grunt\grunt", "right hand", r"characters\grunt\grunt minor plasma pistol"),
    ("elite", CHARACTER_MAP, r"characters\elite\elite", "right hand elite",
     r"characters\elite\elite minor\elite minor plasma rifle"),
]
WEAPONS = [
    ("plasma pistol", CHARACTER_MAP, r"weapons\plasma pistol\plasma pistol"),
    ("plasma rifle", CHARACTER_MAP, r"weapons\plasma rifle\plasma rifle"),
    ("needler", CHARACTER_MAP, r"weapons\needler\needler"),
    ("fuel rod", FUEL_ROD_MAP, r"weapons\fuel rod gun\fuel rod gun"),
]
CHARACTER_LODS = (4, 1, 0)   # superhigh, low, superlow
WEAPON_LODS = (4, 2, 0)

# Order matches HcBpAnim (src/integration/hc_biped_view.h). First name found wins.
ANIM_SLOTS = [
    ("stand pistol idle%0", "stand pistol idle"),
    ("stand pistol move-front%0", "stand pistol move-front"),
    ("stand pistol move-back",),
    ("stand pistol move-left",),
    ("stand pistol move-right",),
    ("crouch pistol idle",),
    ("crouch pistol move-front",),
    ("flee pistol move-front",),
    ("stand pistol airborne",),
    ("stand pistol land-soft",),
    ("stand pistol throw-grenade",),
    ("stand pistol melee",),
    ("stand pistol surprise-front",),
    ("stand pistol warn",),
    ("stand pistol evade-left", "stand pistol dive-left"),
    ("stand pistol evade-right", "stand pistol dive-right"),
    ("h-ping front gut",),
    ("h-ping back gut",),
    ("s-kill front gut%1", "s-kill front gut"),
    ("s-kill back gut", "h-kill back gut"),
    ("s-kill left gut%0", "s-kill left gut"),
    ("s-kill right gut",),
    ("h-kill front gut%0", "h-kill front gut"),
    ("h-kill back gut",),
    ("stand pistol pp fire-1",),                              # overlays
    ("stand pistol pr fire-1", "stand pistol pp fire-1"),
    ("stand pistol ne fire-1", "stand pistol pp fire-1"),
    ("stand missle idle",),                                  # two-handed (fuel rod)
    ("stand missle move-front",),
    ("stand missle move-back",),
    ("stand missle move-left",),
    ("stand missle move-right",),
]
MUZZLE_MARKERS = ("primary trigger", "primary_trigger")
ACTV_CHANGE_COLORS = 0x22C
OBJECT_CHANGE_COLORS = 0x164


def _mid_colour(v):
    c = tuple((v[i] + v[i + 3]) * 0.5 for i in range(3))
    return c if all(0.0 <= x <= 1.0 for x in c) else None


def change_colour(m, actv_path, bipd_path):
    """Primary change colour: the actor variant's, else the biped's default."""
    t = m.find("actv", actv_path)
    if t is not None:
        n, p = m.block(m.tag_data(t) + ACTV_CHANGE_COLORS)
        if n:
            return _mid_colour(m.unpack("6f", p))
    t = m.find("bipd", bipd_path)
    if t is not None:
        n, p = m.block(m.tag_data(t) + OBJECT_CHANGE_COLORS)
        if n:
            pn, pp = m.block(p + 0x20)
            return _mid_colour(m.unpack("6f", pp + 4) if pn else m.unpack("6f", p + 8))
    return None


def build_character(m, name, path, hand_marker, actv, tex, log):
    tag, antr = m.find("mode", path), m.find("antr", path)
    if tag is None or antr is None:
        log("  %s: not in this map" % name)
        return None
    graph = anims.read_anim_graph(m, antr)
    md = skin_pack.ModelData(name, [n.name for n in graph.nodes],
                             [n.parent if n.parent != i else -1 for i, n in enumerate(graph.nodes)])
    colour = change_colour(m, actv, path)
    for lod in CHARACTER_LODS:
        model = models.read_model(m, tag, lod)
        md.add_geometry(m, model, [skin_pack.shader_texture(m, tex, s, colour) for s in model.shaders], log)
        md.end_lod()
    md.slots = [next((graph.get(n) for n in names if graph.get(n)), None) for names in ANIM_SLOTS]

    model = models.read_model(m, tag)
    marks = models.read_markers(m, tag, with_rotation=True)
    if marks.get(hand_marker):
        node, t, q = marks[hand_marker][0]
        if 0 <= node < len(model.nodes):
            md.hand = (md.node_index(model.nodes[node].name), q, t)
    log("  %-14s %s  hand %d  colour %s" % (name, md.summary(), md.hand[0],
                                           tuple(round(c, 2) for c in colour) if colour else None))
    return md


def build_weapon(m, name, path, tex, log):
    tag = m.find("mode", path)
    if tag is None:
        log("  %s: not in this map" % name)
        return None
    md, rest = skin_pack.rigid_skeleton(name)
    md.slots = [rest] + [None] * (len(ANIM_SLOTS) - 1)
    for i, lod in enumerate(WEAPON_LODS):
        model = models.read_model(m, tag, lod)
        if i == 0:
            bind = skin_pack.bind_world(model)
            marks = models.read_markers(m, tag)
            for mk in MUZZLE_MARKERS:
                if marks.get(mk):
                    node, t = marks[mk][0]
                    if 0 <= node < len(bind):
                        md.muzzle = (0, (bind[node] @ np.append(t, 1.0))[:3].astype(np.float32))
                    break
        shaders = [skin_pack.shader_texture(m, tex, s) for s in model.shaders]
        md.add_geometry(m, skin_pack.flatten_to_root(model), shaders, log)
        md.end_lod()
    log("  %-14s %s  muzzle %s" % (name, md.summary(), np.round(md.muzzle[1], 3)))
    return md


def build_pack(load_map, log=print):
    """load_map(name) -> CacheMap (cached by the caller)."""
    tex = None
    out = []
    for name, map_name, path, hand, actv in CHARACTERS:
        m = load_map(map_name)
        tex = tex or skin_pack.Textures(m)
        tex.m = m
        out.append(build_character(m, name, path, hand, actv, tex, log))
    for name, map_name, path in WEAPONS:
        m = load_map(map_name)
        tex.m = m
        out.append(build_weapon(m, name, path, tex, log))
    return skin_pack.write_pack(MAGIC, out, tex, len(ANIM_SLOTS), log), out
