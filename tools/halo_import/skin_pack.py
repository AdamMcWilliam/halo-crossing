"""Skinned model packs: the first-person weapons (HCFP) and the third-person
bipeds and their weapons (HCBP) share one layout.

Layout (little-endian, offsets from the start of the file); the C reader is
src/integration/hc_fp_pack.c and must agree with every size here.

  header   magic[4], u32 version, u32 model_slots, u32 texture_count,
           u32 textures_off, u32 models_off (u32[model_slots], 0 = empty), u32 anim_slots, u32 reserved
  texture  u16 w, u16 h, u16 format (0 CMPR, 1 RGBA8), u16 flags, u32 off, u32 size
  model    u16 nodes, u16 batches, u32 vertices, u32 parents_off (s16[nodes]),
           u32 inv_bind_off (f32[nodes][3][4]), u32 vertices_off, u32 batches_off,
           u32 anims_off (anim[anim_slots]), s16 muzzle_node, s16 hand_node, f32 muzzle[3],
           frame hand (where a held weapon's origin goes, in hand_node space),
           u16 lod_count, u16 pad, u16 lod_batch_end[4], u32 lod_vertex_end[4]
  anim     u16 frames, u16 kind (0 base, 1 overlay, 2 replacement), u32 frames_off,
           u32 root_off (f32[frames][4]: dx dy dz dyaw summed from frame 0, 0 = none),
           f32 root_distance (horizontal root travel over the whole animation)
  vertex   f32 pos[3], f32 normal[3], f32 uv[2], u8 node0, u8 node1 (0xFF none), u16 weight0
  batch    u16 texture (0xFFFF none), u8 vertices, u8 flags, u16 triangles, u16 pad,
           u32 rgba tint, f32 uv_offset[2], u32 indices_off (u16[]), u32 triangles_off (u8[3][])
  frame    per node: f32 quaternion i,j,k,w, f32 translation[3], f32 scale

Level of detail k draws batches [lod_batch_end[k-1], lod_batch_end[k]) over
vertices [lod_vertex_end[k-1], lod_vertex_end[k]); LOD 0 is the most detailed.
"""
import struct

import numpy as np

from . import anims, bitmaps, textures

VERSION = 2
TEX_CMPR, TEX_RGBA8 = 0, 1
BATCH_TRANSLUCENT = 0x1  # blended over the rest of the model, unlit
MAX_BATCH_VERTICES = 120
MAX_LODS = 4
MAX_NODES = 64

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
SOSO_BASE_MAP, SOSO_MULTIPURPOSE_MAP = 0xA4, 0xBC


class Textures:
    """De-duplicated GameCube textures keyed by bitmap tag (and colour change)."""

    def __init__(self, m):
        self.m = m
        self.index = {}
        self.entries = []   # (w, h, fmt, flags, bytes)

    def _add(self, key, entry):
        result = 0xFFFF
        if entry is not None:
            result = len(self.entries)
            self.entries.append(entry)
        self.index[key] = result
        return result

    def get(self, bitmap_tag):
        if bitmap_tag is None:
            return 0xFFFF
        if bitmap_tag.path in self.index:
            return self.index[bitmap_tag.path]
        ents = bitmaps.read_bitmap_entries(self.m, bitmap_tag)
        entry = None
        if ents and ents[0].kind == bitmaps.TYPE_2D:
            bd = ents[0]
            raw = bytes(self.m.m[bd.offset:bd.offset + bd.size])
            if bd.format == bitmaps.FMT_DXT1 and bd.width % 8 == 0 and bd.height % 8 == 0:
                entry = (bd.width, bd.height, TEX_CMPR, 0, textures.dxt1_to_cmpr(raw, bd.width, bd.height))
            else:
                img = bitmaps.decode(self.m, bd)
                if img is not None:
                    entry = _rgba8_entry(img)
        return self._add(bitmap_tag.path, entry)

    def get_change_colour(self, base_tag, multi_tag, colour):
        """Base map with Halo's colour change baked in: where the multipurpose
        map's blue channel is set, the base is multiplied by `colour`."""
        if base_tag is None:
            return 0xFFFF
        if multi_tag is None or colour is None:
            return self.get(base_tag)
        key = (base_tag.path, multi_tag.path, tuple(round(c, 3) for c in colour))
        if key in self.index:
            return self.index[key]
        base, multi = _decode(self.m, base_tag), _decode(self.m, multi_tag)
        if base is None or multi is None:
            return self.get(base_tag)
        if multi.shape[:2] != base.shape[:2]:
            ys = np.arange(base.shape[0]) * multi.shape[0] // base.shape[0]
            xs = np.arange(base.shape[1]) * multi.shape[1] // base.shape[1]
            multi = multi[ys][:, xs]
        mask = multi[..., 2:3].astype(np.float32) / 255.0
        tint = 1.0 + (np.array(colour, np.float32)[None, None, :] - 1.0) * mask
        img = base.copy()
        img[..., :3] = np.clip(base[..., :3].astype(np.float32) * tint + 0.5, 0, 255).astype(np.uint8)
        h, w = img.shape[:2]
        if w % 8 == 0 and h % 8 == 0 and (img[..., 3] >= 250).all():
            entry = (w, h, TEX_CMPR, 0, textures.dxt1_to_cmpr(textures.encode_dxt1(img), w, h))
        else:
            entry = _rgba8_entry(img)
        return self._add(key, entry)


def _decode(m, tag):
    ents = bitmaps.read_bitmap_entries(m, tag)
    return bitmaps.decode(m, ents[0]) if ents else None


def _rgba8_entry(img):
    img = textures.downscale(img, RGBA8_MAX_DIM)
    h, w = img.shape[0] // 4 * 4, img.shape[1] // 4 * 4
    img = np.ascontiguousarray(img[:h, :w])
    return (w, h, TEX_RGBA8, int((img[..., 3] < 250).any()), textures.rgba_to_gc_rgba8(img))


def shader_texture(m, tex, shader, colour=None):
    """(texture index, flags, tint) for one of a model's shaders."""
    flags, tint = SHADER_CLASS.get(shader.group if shader else "", DEFAULT_CLASS)
    if shader is not None and shader.group == "soso" and colour is not None:
        o = m.tag_data(shader)
        change_source = m.unpack("H", o + 0x4C)[0]
        if change_source:
            return tex.get_change_colour(m.ref(o + SOSO_BASE_MAP), m.ref(o + SOSO_MULTIPURPOSE_MAP), colour), flags, tint
    return tex.get(bitmaps.base_map_of_shader(m, shader)), flags, tint


def bind_world(model):
    return anims.world_transforms([n.parent for n in model.nodes],
                                  [n.rotation for n in model.nodes],
                                  [n.translation for n in model.nodes])


def split_batches(tris, limit):
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


class ModelData:
    """One pack entry. `skeleton` is the list of node names (parents precede children)."""

    def __init__(self, name, skeleton, parents):
        self.name = name
        self.skeleton = list(skeleton)
        self.parents = list(parents)
        self.inv_bind = np.tile(np.eye(4), (len(skeleton), 1, 1))
        self.verts, self.batches = [], []
        self.lod_ends = []           # (batch end, vertex end) per LOD
        self.slots = []
        self.muzzle = (-1, np.zeros(3, np.float32))
        self.hand = (-1, np.array([0, 0, 0, 1], np.float32), np.zeros(3, np.float32))

    def node_index(self, name):
        try:
            return self.skeleton.index(name)
        except ValueError:
            return -1

    def add_geometry(self, m, model, shader_tex, log=print):
        """Append a 'mode' model's parts, skinned to this skeleton by node name.
        shader_tex: [(texture, flags, tint)] per model shader."""
        bind = bind_world(model)
        remap = np.full(len(model.nodes), -1, np.int32)
        for i, n in enumerate(model.nodes):
            ai = self.node_index(n.name)
            if ai < 0:
                log("  %s: model node %r not in the skeleton" % (self.name, n.name))
                continue
            remap[i] = ai
            self.inv_bind[ai] = np.linalg.inv(bind[i])
        for part in model.parts:
            base = len(self.verts)
            uv = part.uvs * np.array([model.u_scale, model.v_scale], np.float32)
            for k in range(len(part.positions)):
                n0 = remap[part.node0[k]] if 0 <= part.node0[k] < len(remap) else -1
                n1 = remap[part.node1[k]] if 0 <= part.node1[k] < len(remap) else -1
                w = float(part.weight[k]) if n1 >= 0 else 1.0
                self.verts.append((part.positions[k], part.normals[k], uv[k], max(n0, 0),
                                   n1 if n1 >= 0 else 0xFF, w))
            t_index, flags, tint = (shader_tex[part.shader] if 0 <= part.shader < len(shader_tex)
                                    else (0xFFFF,) + DEFAULT_CLASS)
            for local, tris in split_batches(part.triangles.tolist(), MAX_BATCH_VERTICES):
                idx = [base + v for v in local]
                u = uv[np.array(local)]
                off = np.round((u.min(0) + u.max(0)) * 0.5)
                self.batches.append((t_index, flags, tint, off, idx, tris))

    def end_lod(self):
        if len(self.lod_ends) >= MAX_LODS:
            raise ValueError("%s: too many LODs" % self.name)
        self.lod_ends.append((len(self.batches), len(self.verts)))

    def summary(self):
        lods = "/".join(str(v - (self.lod_ends[i - 1][1] if i else 0)) for i, (_, v) in enumerate(self.lod_ends))
        blended = sum(1 for b in self.batches if b[1] & BATCH_TRANSLUCENT)
        return "nodes %2d  verts %s  batches %3d (%d blended)  anims %s" % (
            len(self.skeleton), lods, len(self.batches), blended, "".join("x" if a else "." for a in self.slots))


def rigid_skeleton(name):
    """A one-node skeleton for models drawn whole (third-person weapons)."""
    md = ModelData(name, ["root"], [-1])
    rest = anims.Animation("rest", 0, 1, np.array([[[0, 0, 0, 1]]], np.float32),
                           np.zeros((1, 1, 3), np.float32), np.ones((1, 1), np.float32))
    return md, rest


def flatten_to_root(model):
    """Collapse a model onto one node at its bind pose (vertices are already
    in model space), so it can be drawn as a rigid object."""
    for n in model.nodes:
        n.name = "root"
    for part in model.parts:
        part.node0 = np.zeros_like(part.node0)
        part.node1 = np.full_like(part.node1, -1)
        part.weight = np.ones_like(part.weight)
    root = model.nodes[0]
    root.parent, root.translation, root.rotation = -1, np.zeros(3, np.float32), np.array([0, 0, 0, 1], np.float32)
    model.nodes = [root]
    return model


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


def _write_model(w, md, anim_slots):
    n = len(md.skeleton)
    if not 0 < n <= MAX_NODES:
        raise ValueError("%s: %d nodes" % (md.name, n))
    if len(md.slots) != anim_slots:
        raise ValueError("%s: %d animation slots, expected %d" % (md.name, len(md.slots), anim_slots))
    lods = md.lod_ends or [(len(md.batches), len(md.verts))]
    w.align(4)
    parents = w.put(struct.pack("<%dh" % n, *md.parents))
    w.align(4)
    inv = w.put(np.ascontiguousarray(md.inv_bind[:, :3, :4], np.float32).tobytes())
    vtx = bytearray()
    for pos, nrm, uv, n0, n1, wt in md.verts:
        vtx += struct.pack("<8f2BH", *pos, *nrm, *uv, n0, n1, int(round(wt * 65535)))
    w.align(4)
    verts = w.put(vtx)
    data = []
    for tex_i, flags, tint, off, idx, tris in md.batches:
        w.align(4)
        io = w.put(struct.pack("<%dH" % len(idx), *idx))
        to = w.put(bytes(v for t in tris for v in t))
        data.append((tex_i, len(idx), flags, len(tris), tint, off, io, to))
    w.align(4)
    batches = w.put(b"".join(struct.pack("<HBBHHI2fII", t, nv, f, nt, 0, c, float(o[0]), float(o[1]), io, to)
                             for t, nv, f, nt, c, o, io, to in data))
    slot_data = []
    for a in md.slots:
        if a is None:
            slot_data.append((0, 0, 0, 0, 0.0))
            continue
        frames = np.concatenate([a.rotations, a.translations, a.scales[..., None]], axis=2).astype(np.float32)
        w.align(4)
        fo = w.put(frames.tobytes())
        ro, dist = 0, 0.0
        if a.root is not None:
            cum = np.cumsum(a.root, axis=0, dtype=np.float64)
            cum = np.concatenate([np.zeros((1, 4)), cum[:-1]], axis=0).astype(np.float32)
            ro = w.put(cum.tobytes())
            dist = float(np.linalg.norm(a.root[:, :2], axis=1).sum())
        slot_data.append((a.frames, a.kind, fo, ro, dist))
    w.align(4)
    anims_off = w.put(b"".join(struct.pack("<HHIIf", *s) for s in slot_data))
    lod_b = [b for b, _ in lods] + [0] * (MAX_LODS - len(lods))
    lod_v = [v for _, v in lods] + [0] * (MAX_LODS - len(lods))
    w.align(4)
    head = w.put(struct.pack("<HHIIIIII hh3f 8f HH4H4I", n, len(md.batches), len(md.verts), parents, inv, verts,
                             batches, anims_off, md.muzzle[0], md.hand[0], *[float(x) for x in md.muzzle[1]],
                             *[float(x) for x in md.hand[1]], *[float(x) for x in md.hand[2]], 1.0,
                             len(lods), 0, *lod_b, *lod_v))
    return head


def write_pack(magic, models, tex, anim_slots, log=print):
    """models: list of ModelData or None per slot."""
    w = _Writer()
    w.put(b"\0" * 32)
    tex_table = w.tell()
    w.put(b"\0" * (16 * len(tex.entries)))
    model_table = w.tell()
    w.put(b"\0" * (4 * len(models)))
    for i, (tw, th, fmt, flags, data) in enumerate(tex.entries):
        w.align(32)
        off = w.put(data)
        w.patch(tex_table + 16 * i, "4H2I", tw, th, fmt, flags, off, len(data))
    for i, md in enumerate(models):
        if md is not None:
            w.patch(model_table + 4 * i, "I", _write_model(w, md, anim_slots))
    w.buf[0:4] = magic
    w.patch(4, "7I", VERSION, len(models), len(tex.entries), tex_table, model_table, anim_slots, 0)
    tex_bytes = sum(len(e[4]) for e in tex.entries)
    log("  %d textures (%.1f MB), pack %.1f MB" % (len(tex.entries), tex_bytes / 1e6, len(w.buf) / 1e6))
    return bytes(w.buf)
