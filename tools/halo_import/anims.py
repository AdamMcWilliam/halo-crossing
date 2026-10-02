"""Xbox 'antr' (model_animations) tags, uncompressed animations.

Main struct: +0x48 first-person weapons block, +0x68 nodes (64 bytes each),
+0x74 animations (180 bytes each). Animation element:
  +0x00 name[32]  +0x20 u16 type (0 base, 1 overlay, 2 replacement)
  +0x22 u16 frame count  +0x24 u16 frame size
  +0x26 u16 frame info type (0 none, 1 dx dy, 2 dx dy dyaw, 3 dx dy dz dyaw)
  +0x2C u16 node count  +0x3A u16 flags (bit 0: compressed)
  +0x48 data ref frame info (root motion, one delta per frame)
  +0x5C u32[2] translation flags  +0x6C u32[2] rotation flags  +0x7C u32[2] scale flags
  +0x8C data ref default data  +0xA0 data ref frame data
Per frame, per node in order: [rotation s16 x4 /32767 if animated]
[translation float3 if animated] [scale float if animated]. The default
data holds the same three channels for every node where they are not animated.
"""
import numpy as np

NODE_SIZE = 64
ANIM_SIZE = 180
FLAG_COMPRESSED = 0x1


class AnimNode:
    def __init__(self, name, parent):
        self.name, self.parent = name, parent


class Animation:
    def __init__(self, name, kind, frames, rotations, translations, scales, root=None):
        self.name, self.kind = name, kind
        self.frames = frames
        self.rotations = rotations        # (frames, nodes, 4) i, j, k, w
        self.translations = translations  # (frames, nodes, 3)
        self.scales = scales              # (frames, nodes)
        self.root = root                  # (frames, 4) dx, dy, dz, dyaw per frame, or None


class AnimGraph:
    def __init__(self, path, nodes, animations):
        self.path, self.nodes, self.animations = path, nodes, animations

    def get(self, name):
        for a in self.animations:
            if a.name == name:
                return a
        return None


def _flags(m, off, count):
    lo, hi = m.unpack("2I", off)
    bits = lo | (hi << 32)
    return [(bits >> i) & 1 == 1 for i in range(count)]


def _read_animation(m, e, n_nodes):
    name = bytes(m.m[e:e + 32]).split(b"\0", 1)[0].decode("latin-1")
    kind, frames, frame_size = m.unpack("3H", e + 0x20)
    node_count = m.unpack("H", e + 0x2C)[0]
    flags = m.unpack("H", e + 0x3A)[0]
    if flags & FLAG_COMPRESSED or node_count != n_nodes or frames == 0:
        return None
    tf = _flags(m, e + 0x5C, node_count)
    rf = _flags(m, e + 0x6C, node_count)
    sf = _flags(m, e + 0x7C, node_count)
    d_size, d_off = m.data_ref(e + 0x8C)
    f_size, f_off = m.data_ref(e + 0xA0)
    expect = sum(8 * r + 12 * t + 4 * s for r, t, s in zip(rf, tf, sf))
    if expect != frame_size or f_size < frames * frame_size:
        raise ValueError(f"{name}: frame size {frame_size}, flags imply {expect}")

    rot = np.zeros((node_count, 4), np.float32)
    rot[:, 3] = 1.0
    trans = np.zeros((node_count, 3), np.float32)
    scale = np.ones(node_count, np.float32)
    p = d_off
    for i in range(node_count):
        if not rf[i]:
            rot[i] = np.array(m.unpack("4h", p), np.float32) / 32767.0
            p += 8
        if not tf[i]:
            trans[i] = m.unpack("3f", p)
            p += 12
        if not sf[i]:
            scale[i] = m.unpack("f", p)[0]
            p += 4
    rots = np.repeat(rot[None], frames, 0)
    transl = np.repeat(trans[None], frames, 0)
    scales = np.repeat(scale[None], frames, 0)
    for f in range(frames):
        p = f_off + f * frame_size
        for i in range(node_count):
            if rf[i]:
                rots[f, i] = np.array(m.unpack("4h", p), np.float32) / 32767.0
                p += 8
            if tf[i]:
                transl[f, i] = m.unpack("3f", p)
                p += 12
            if sf[i]:
                scales[f, i] = m.unpack("f", p)[0]
                p += 4
    norm = np.linalg.norm(rots, axis=2, keepdims=True)
    rots = rots / np.maximum(norm, 1e-6)
    return Animation(name, kind, frames, rots, transl, scales, _read_root(m, e, frames))


_ROOT_FIELDS = {1: ("dx", "dy"), 2: ("dx", "dy", "dyaw"), 3: ("dx", "dy", "dz", "dyaw")}


def _read_root(m, e, frames):
    fields = _ROOT_FIELDS.get(m.unpack("H", e + 0x26)[0])
    if fields is None:
        return None
    size, off = m.data_ref(e + 0x48)
    if size < frames * 4 * len(fields):
        return None
    raw = np.frombuffer(m.m, "<f4", frames * len(fields), off).reshape(frames, len(fields))
    root = np.zeros((frames, 4), np.float32)
    for c, f in enumerate(fields):
        root[:, ("dx", "dy", "dz", "dyaw").index(f)] = raw[:, c]
    return root


def read_anim_graph(m, tag):
    o = m.tag_data(tag)
    n_nodes, no = m.block(o + 0x68)
    nodes = []
    for i in range(n_nodes):
        e = no + i * NODE_SIZE
        name = bytes(m.m[e:e + 32]).split(b"\0", 1)[0].decode("latin-1")
        parent = m.unpack("h", e + 0x24)[0]
        nodes.append(AnimNode(name, parent))
    anims = []
    n_an, an = m.block(o + 0x74)
    for i in range(n_an):
        a = _read_animation(m, an + i * ANIM_SIZE, n_nodes)
        if a is not None:
            anims.append(a)
    return AnimGraph(tag.path, nodes, anims)


# -- pose maths ----------------------------------------------------------

def quat_to_matrix(q):
    """Halo quaternions (i, j, k, w) -> 3x3. Halo stores the inverse rotation."""
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)],
        [2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)],
        [2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)],
    ], np.float64)


def world_transforms(parents, rotations, translations, scales=None):
    """Local node transforms -> list of 4x4 world matrices (parents precede children)."""
    out = [None] * len(parents)
    for i, par in enumerate(parents):
        m = np.eye(4)
        m[:3, :3] = quat_to_matrix(rotations[i]) * (scales[i] if scales is not None else 1.0)
        m[:3, 3] = translations[i]
        out[i] = m if par < 0 or par == i else out[par] @ m
    return out
