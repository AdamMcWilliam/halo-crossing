"""Xbox 'mode' (model) tags: nodes, geometry parts, shaders.

Xbox parts keep their geometry outside the usual tag blocks:
  part +0x48 u32  strip index count     +0x4C ptr  u16 triangle strip
  part +0x58 u32  vertex count          +0x64 ptr  vertex buffer descriptor,
                                                    whose +4 is a ptr to the vertices
Compressed vertex (32 bytes): float3 position (model space), u32 normal
(11/11/10 signed), u32 binormal, u32 tangent, s16 u, s16 v (/32767),
s8 node0*3, s8 node1*3, s16 node0 weight (/32767).
"""
import numpy as np

NODE_SIZE = 156
GEOMETRY_SIZE = 48
PART_SIZE = 0x68
REGION_SIZE = 76
PERMUTATION_SIZE = 88
SHADER_REF_SIZE = 32

VERTEX_DTYPE = np.dtype([
    ("pos", "<f4", 3), ("normal", "<u4"), ("binormal", "<u4"), ("tangent", "<u4"),
    ("uv", "<i2", 2), ("node0", "i1"), ("node1", "i1"), ("weight", "<i2"),
])
assert VERTEX_DTYPE.itemsize == 32


def unpack_normal(packed):
    """11/11/10 signed fixed point -> (N, 3) float."""
    p = packed.astype(np.int64)
    x = (p & 0x7FF).astype(np.int32)
    y = ((p >> 11) & 0x7FF).astype(np.int32)
    z = ((p >> 22) & 0x3FF).astype(np.int32)
    x = np.where(x >= 1024, x - 2048, x) / 1023.0
    y = np.where(y >= 1024, y - 2048, y) / 1023.0
    z = np.where(z >= 512, z - 1024, z) / 511.0
    n = np.stack([x, y, z], axis=1)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-6)


def strip_to_triangles(strip):
    tris = []
    for i in range(2, len(strip)):
        a, b, c = strip[i - 2], strip[i - 1], strip[i]
        if a == b or b == c or a == c:
            continue
        tris.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return np.array(tris, np.int32).reshape(-1, 3)


class Node:
    def __init__(self, name, next_sibling, child, parent, translation, rotation):
        self.name, self.next, self.child, self.parent = name, next_sibling, child, parent
        self.translation = np.array(translation, np.float32)
        self.rotation = np.array(rotation, np.float32)  # quaternion i, j, k, w


class Part:
    def __init__(self, shader, positions, normals, uvs, node0, node1, weight, triangles):
        self.shader = shader
        self.positions, self.normals, self.uvs = positions, normals, uvs
        self.node0, self.node1, self.weight = node0, node1, weight
        self.triangles = triangles


class Model:
    def __init__(self, path, u_scale, v_scale, nodes, parts, shaders):
        self.path, self.u_scale, self.v_scale = path, u_scale, v_scale
        self.nodes, self.parts, self.shaders = nodes, parts, shaders


def _read_part(m, p):
    shader = m.unpack("h", p + 0x04)[0]
    strip_count = m.u32(p + 0x48)
    strip = np.frombuffer(m.m, "<u2", strip_count, m.addr(m.u32(p + 0x4C)))
    vcount = m.u32(p + 0x58)
    vdesc = m.addr(m.u32(p + 0x64))
    verts = np.frombuffer(m.m, VERTEX_DTYPE, vcount, m.addr(m.u32(vdesc + 4)))
    node0 = verts["node0"].astype(np.int32) // 3
    node1 = verts["node1"].astype(np.int32)
    node1 = np.where(node1 < 0, -1, node1 // 3)
    weight = np.clip(verts["weight"].astype(np.float32) / 32767.0, 0.0, 1.0)
    return Part(shader, verts["pos"].copy(), unpack_normal(verts["normal"]),
                verts["uv"].astype(np.float32) / 32767.0, node0, node1, weight,
                strip_to_triangles(strip))


def _geometry_parts(m, geo):
    n_parts, parts = m.block(geo + 0x24)
    return [_read_part(m, parts + i * PART_SIZE) for i in range(n_parts)]


def read_model(m, tag):
    o = m.tag_data(tag)
    u_scale, v_scale = m.unpack("2f", o + 0x30)
    if u_scale == 0.0:
        u_scale = 1.0
    if v_scale == 0.0:
        v_scale = 1.0

    nodes = []
    n_nodes, no = m.block(o + 0xB8)
    for i in range(n_nodes):
        e = no + i * NODE_SIZE
        name = bytes(m.m[e:e + 32]).split(b"\0", 1)[0].decode("latin-1")
        nxt, child, parent = m.unpack("3h", e + 0x20)
        t = m.unpack("3f", e + 0x28)
        q = m.unpack("4f", e + 0x34)
        nodes.append(Node(name, nxt, child, parent, t, q))

    n_geo, geo = m.block(o + 0xD0)
    # Highest-detail geometry of each region's first permutation.
    wanted = []
    n_reg, reg = m.block(o + 0xC4)
    for r in range(n_reg):
        n_perm, perm = m.block(reg + r * REGION_SIZE + 0x40)
        if n_perm:
            lods = m.unpack("5h", perm + 0x40)
            g = next((x for x in reversed(lods) if 0 <= x < n_geo), -1)
            if g >= 0 and g not in wanted:
                wanted.append(g)
    if not wanted:
        wanted = list(range(n_geo))
    parts = []
    for g in wanted:
        parts.extend(_geometry_parts(m, geo + g * GEOMETRY_SIZE))

    shaders = []
    n_sh, sh = m.block(o + 0xDC)
    for i in range(n_sh):
        shaders.append(m.ref(sh + i * SHADER_REF_SIZE))
    return Model(tag.path, u_scale, v_scale, nodes, parts, shaders)
