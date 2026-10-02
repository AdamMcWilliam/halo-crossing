"""First-person weapon pack: Chief's arms + each weapon, skinned, with animations.

Layout (little-endian, offsets from the start of the file); the C reader is
src/integration/hc_fp_pack.c and must agree with every size here.

  header   "HCFP" u32 version, u32 weapon_slots, u32 texture_count,
           u32 textures_off, u32 weapons_off, u32 anim_slots, u32 reserved
  texture  u16 w, u16 h, u16 format (0 CMPR, 1 RGBA8), u16 flags, u32 off, u32 size
  weapon   u16 nodes, u16 batches, u32 vertices, u32 parents_off (s16[nodes]),
           u32 inv_bind_off (f32[nodes][3][4]), u32 vertices_off, u32 batches_off,
           u32 anims_off (anim_slots x {u16 frames, u16 flags, u32 off}),
           s16 muzzle_node, u16 pad, f32 muzzle[3]
  vertex   f32 pos[3], f32 normal[3], f32 uv[2], u8 node0, u8 node1 (0xFF none), u16 weight0
  batch    u16 texture (0xFFFF none), u8 vertices, u8 flags, u16 triangles, u16 pad,
           u32 rgba tint, f32 uv_offset[2], u32 indices_off (u16[]), u32 triangles_off (u8[3][])
  frame    per node: f32 quaternion i,j,k,w, f32 translation[3], f32 scale

Positions are Halo first-person camera space in world units: x forward, y left, z up.
"""
import struct

import numpy as np

from . import anims, bitmaps, models, textures

VERSION = 1
MAGIC = b"HCFP"
TEX_CMPR, TEX_RGBA8 = 0, 1
BATCH_TRANSLUCENT = 0x1  # blended over the rest of the viewmodel, unlit
MAX_BATCH_VERTICES = 120

# Shader group -> (flags, tint). Only the base map is imported: environment
# shaders draw lit, self-illuminated screens draw flat, the rest blend.
SHADER_CLASS = {
    "soso": (0, 0xFFFFFFFF),
    "schi": (0, 0xFFFFFFFF),
    "scex": (BATCH_TRANSLUCENT, 0xFFFFFFC0),
    "sotr": (BATCH_TRANSLUCENT, 0xFFFFFFA0),
    "smet": (BATCH_TRANSLUCENT, 0xFFFFFFC0),
    "sgla": (BATCH_TRANSLUCENT, 0xFF78E6B4),  # needler crystals: cube-mapped glass
}
DEFAULT_CLASS = (BATCH_TRANSLUCENT, 0xFFFFFF90)
RGBA8_MAX_DIM = 256

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
]

ARMS = r"characters\cyborg\fp\fp"
MUZZLE_MARKERS = ("primary trigger", "primary_trigger")


def _fp_path(weapon):
    return "weapons\\%s\\fp\\fp" % weapon


class Textures:
    """De-duplicated GameCube textures keyed by bitmap tag."""

    def __init__(self, m):
        self.m = m
        self.index = {}
        self.entries = []   # (w, h, fmt, flags, bytes)

    def get(self, bitmap_tag):
        if bitmap_tag is None:
            return 0xFFFF
        if bitmap_tag.path in self.index:
            return self.index[bitmap_tag.path]
        ents = bitmaps.read_bitmap_entries(self.m, bitmap_tag)
        result = 0xFFFF
        if ents and ents[0].kind == bitmaps.TYPE_2D:
            bd = ents[0]
            raw = bytes(self.m.m[bd.offset:bd.offset + bd.size])
            if bd.format == bitmaps.FMT_DXT1 and bd.width % 8 == 0 and bd.height % 8 == 0:
                entry = (bd.width, bd.height, TEX_CMPR, 0, textures.dxt1_to_cmpr(raw, bd.width, bd.height))
            else:
                img = bitmaps.decode(self.m, bd)
                if img is None:
                    entry = None
                else:
                    img = textures.downscale(img, RGBA8_MAX_DIM)
                    h, w = img.shape[0] // 4 * 4, img.shape[1] // 4 * 4
                    img = np.ascontiguousarray(img[:h, :w])
                    entry = (w, h, TEX_RGBA8, int((img[..., 3] < 250).any()), textures.rgba_to_gc_rgba8(img))
            if entry is not None:
                result = len(self.entries)
                self.entries.append(entry)
        self.index[bitmap_tag.path] = result
        return result


def _bind_world(model):
    return anims.world_transforms([n.parent for n in model.nodes],
                                  [n.rotation for n in model.nodes],
                                  [n.translation for n in model.nodes])


def _batches(tris, limit):
    """Greedy split of a triangle list into chunks referencing <= limit vertices."""
    out, local, cur = [], {}, []
    for t in tris:
        new = [v for v in dict.fromkeys(t) if v not in local]
        if len(local) + len(new) > limit:
            out.append((list(local), cur))
            local, cur = {}, []
            new = list(dict.fromkeys(t))
        for v in new:
            local[v] = len(local)
        cur.append(tuple(local[v] for v in t))
    if cur:
        out.append((list(local), cur))
    return out


class WeaponData:
    pass


def build_weapon(m, weapon, arms, tex, log):
    gun_tag = m.find("mode", _fp_path(weapon))
    antr_tag = m.find("antr", _fp_path(weapon))
    if gun_tag is None or antr_tag is None:
        log("  %s: no first-person model in this map" % weapon)
        return None
    gun = models.read_model(m, gun_tag)
    graph = anims.read_anim_graph(m, antr_tag)
    by_name = {n.name: i for i, n in enumerate(graph.nodes)}
    n_nodes = len(graph.nodes)
    if n_nodes > 254:
        raise ValueError("%s: %d nodes" % (weapon, n_nodes))

    inv_bind = np.tile(np.eye(4), (n_nodes, 1, 1))
    verts, batches = [], []
    for model in (arms, gun):
        bind = _bind_world(model)
        remap = np.full(len(model.nodes), -1, np.int32)
        for i, n in enumerate(model.nodes):
            ai = by_name.get(n.name)
            if ai is None:
                log("  %s: model node %r not animated" % (weapon, n.name))
                continue
            remap[i] = ai
            inv_bind[ai] = np.linalg.inv(bind[i])
        shader_tex = []
        for s in model.shaders:
            flags, tint = SHADER_CLASS.get(s.group if s else "", DEFAULT_CLASS)
            shader_tex.append((tex.get(bitmaps.base_map_of_shader(m, s)), flags, tint))
        for part in model.parts:
            base = len(verts)
            uv = part.uvs * np.array([model.u_scale, model.v_scale], np.float32)
            for k in range(len(part.positions)):
                n0 = remap[part.node0[k]] if 0 <= part.node0[k] < len(remap) else -1
                n1 = remap[part.node1[k]] if 0 <= part.node1[k] < len(remap) else -1
                w = float(part.weight[k]) if n1 >= 0 else 1.0
                verts.append((part.positions[k], part.normals[k], uv[k], max(n0, 0), n1 if n1 >= 0 else 0xFF, w))
            t_index, flags, tint = (shader_tex[part.shader] if 0 <= part.shader < len(shader_tex)
                                    else (0xFFFF,) + DEFAULT_CLASS)
            for local, tris in _batches(part.triangles.tolist(), MAX_BATCH_VERTICES):
                idx = [base + v for v in local]
                u = uv[np.array(local)]
                off = np.round((u.min(0) + u.max(0)) * 0.5)
                batches.append((t_index, flags, tint, off, idx, tris))

    slots = []
    for names in ANIM_SLOTS:
        a = next((graph.get("first-person " + n) for n in names if graph.get("first-person " + n)), None)
        slots.append(a)

    muzzle = (-1, np.zeros(3, np.float32))
    marks = models.read_markers(m, gun_tag)
    for name in MUZZLE_MARKERS:
        if marks.get(name):
            node, t = marks[name][0]
            if 0 <= node < len(gun.nodes) and gun.nodes[node].name in by_name:
                muzzle = (by_name[gun.nodes[node].name], t)
            break

    wd = WeaponData()
    wd.name, wd.parents = weapon, [n.parent if n.parent != i else -1 for i, n in enumerate(graph.nodes)]
    wd.inv_bind, wd.verts, wd.batches, wd.slots, wd.muzzle = inv_bind, verts, batches, slots, muzzle
    blended = sum(1 for b in batches if b[1] & BATCH_TRANSLUCENT)
    log("  %-16s nodes %2d  verts %5d  batches %3d (%d blended)  anims %s  muzzle %s" % (
        weapon, n_nodes, len(verts), len(batches), blended,
        "".join("x" if a else "." for a in slots), muzzle[0]))
    return wd


class _Writer:
    def __init__(self):
        self.buf = bytearray()

    def tell(self):
        return len(self.buf)

    def align(self, n):
        self.buf.extend(b"\0" * (-len(self.buf) % n))

    def put(self, data):
        off = len(self.buf)
        self.buf.extend(data)
        return off

    def patch(self, off, fmt, *vals):
        struct.pack_into("<" + fmt, self.buf, off, *vals)


def _write_weapon(w, wd):
    n = len(wd.parents)
    w.align(4)
    parents = w.put(struct.pack("<%dh" % n, *wd.parents))
    w.align(4)
    inv = w.put(np.ascontiguousarray(wd.inv_bind[:, :3, :4], np.float32).tobytes())
    vtx = bytearray()
    for pos, nrm, uv, n0, n1, wt in wd.verts:
        vtx += struct.pack("<8f2BH", *pos, *nrm, *uv, n0, n1, int(round(wt * 65535)))
    w.align(4)
    verts = w.put(vtx)
    data = []
    for tex_i, flags, tint, off, idx, tris in wd.batches:
        w.align(4)
        io = w.put(struct.pack("<%dH" % len(idx), *idx))
        to = w.put(bytes(v for t in tris for v in t))
        data.append((tex_i, len(idx), flags, len(tris), tint, off, io, to))
    w.align(4)
    batches = w.put(b"".join(struct.pack("<HBBHHI2fII", t, nv, f, nt, 0, c, float(o[0]), float(o[1]), io, to)
                             for t, nv, f, nt, c, o, io, to in data))
    slot_data = []
    for a in wd.slots:
        if a is None:
            slot_data.append((0, 0))
            continue
        frames = np.concatenate([a.rotations, a.translations, a.scales[..., None]], axis=2).astype(np.float32)
        w.align(4)
        slot_data.append((a.frames, w.put(frames.tobytes())))
    w.align(4)
    anims_off = w.put(b"".join(struct.pack("<HHI", f, 0, o) for f, o in slot_data))
    w.align(4)
    head = w.put(struct.pack("<HHIIIIII hH3f", n, len(wd.batches), len(wd.verts), parents, inv, verts,
                             batches, anims_off, wd.muzzle[0], 0, *[float(x) for x in wd.muzzle[1]]))
    return head


def build_pack(m, log=print):
    arms_tag = m.find("mode", ARMS)
    if arms_tag is None:
        raise ValueError("map has no first-person arms (%s)" % ARMS)
    arms = models.read_model(m, arms_tag)
    tex = Textures(m)
    weapons = [build_weapon(m, name, arms, tex, log) if name else None for name in WEAPONS]

    w = _Writer()
    w.put(b"\0" * 32)
    tex_table = w.tell()
    w.put(b"\0" * (16 * len(tex.entries)))
    weapon_table = w.tell()
    w.put(b"\0" * (4 * len(WEAPONS)))
    for i, (tw, th, fmt, flags, data) in enumerate(tex.entries):
        w.align(32)
        off = w.put(data)
        w.patch(tex_table + 16 * i, "4H2I", tw, th, fmt, flags, off, len(data))
    for i, wd in enumerate(weapons):
        if wd is not None:
            w.patch(weapon_table + 4 * i, "I", _write_weapon(w, wd))
    w.buf[0:4] = MAGIC
    w.patch(4, "7I", VERSION, len(WEAPONS), len(tex.entries), tex_table, weapon_table, len(ANIM_SLOTS), 0)
    tex_bytes = sum(len(e[4]) for e in tex.entries)
    log("  %d textures (%.1f MB), pack %.1f MB" % (len(tex.entries), tex_bytes / 1e6, len(w.buf) / 1e6))
    return bytes(w.buf), weapons
