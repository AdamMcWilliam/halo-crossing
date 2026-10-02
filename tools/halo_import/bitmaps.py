"""Xbox 'bitm' tags -> RGBA images (mip 0).

Bitmap data element (48 bytes, block at bitm +0x60):
  +0x00 'bitm'  +0x04 u16 width  +0x06 height  +0x08 depth  +0x0A type
  +0x0C u16 format  +0x0E u16 flags  +0x14 u16 mipmaps  +0x18 u32 pixel offset
  +0x1C u32 pixel size. On Xbox the pixel offset points into the same map.
"""
import numpy as np

from . import textures

FMT_A8, FMT_Y8, FMT_AY8, FMT_A8Y8 = 0, 1, 2, 3
FMT_R5G6B5, FMT_A1R5G5B5, FMT_A4R4G4B4 = 6, 8, 9
FMT_X8R8G8B8, FMT_A8R8G8B8 = 10, 11
FMT_DXT1, FMT_DXT3, FMT_DXT5 = 14, 15, 16
FMT_P8 = 17
FLAG_SWIZZLED = 0x8
TYPE_2D = 0

_BPP = {FMT_A8: 1, FMT_Y8: 1, FMT_AY8: 1, FMT_A8Y8: 2, FMT_R5G6B5: 2, FMT_A1R5G5B5: 2,
        FMT_A4R4G4B4: 2, FMT_X8R8G8B8: 4, FMT_A8R8G8B8: 4, FMT_P8: 1}
_DXT = {FMT_DXT1: "dxt1", FMT_DXT3: "dxt3", FMT_DXT5: "dxt5"}


class BitmapData:
    def __init__(self, width, height, depth, kind, fmt, flags, mipmaps, offset, size):
        self.width, self.height, self.depth = width, height, depth
        self.kind, self.format, self.flags = kind, fmt, flags
        self.mipmaps, self.offset, self.size = mipmaps, offset, size


def read_bitmap_entries(m, tag):
    o = m.tag_data(tag)
    count, el = m.block(o + 0x60)
    out = []
    for i in range(count):
        e = el + i * 48
        _, w, h, d, kind, fmt, flags = m.unpack("I6H", e)
        mips = m.unpack("H", e + 0x14)[0]
        off, size = m.unpack("2I", e + 0x18)
        out.append(BitmapData(w, h, d, kind, fmt, flags, mips, off, size))
    return out


def _expand(v, bits):
    return (v * 255 // ((1 << bits) - 1)).astype(np.uint8)


def decode(m, bd):
    """Mip 0 of a 2D bitmap as (h, w, 4) uint8 RGBA, or None if unsupported."""
    if bd.kind != TYPE_2D:
        return None
    w, h = bd.width, bd.height
    raw = bytes(m.m[bd.offset:bd.offset + bd.size])
    if bd.format in _DXT:
        return textures.decode_dxt(raw, w, h, _DXT[bd.format])
    bpp = _BPP.get(bd.format)
    if bpp is None:
        return None
    if bd.flags & FLAG_SWIZZLED:
        px = textures.unswizzle(raw, w, h, bpp)
    else:
        px = np.frombuffer(raw, np.uint8, w * h * bpp).reshape(h, w, bpp)
    out = np.zeros((h, w, 4), np.uint8)
    if bpp == 4:  # B, G, R, A in memory
        out[..., 0], out[..., 1], out[..., 2] = px[..., 2], px[..., 1], px[..., 0]
        out[..., 3] = px[..., 3] if bd.format == FMT_A8R8G8B8 else 255
    elif bpp == 2:
        v = px[..., 0].astype(np.uint32) | (px[..., 1].astype(np.uint32) << 8)
        if bd.format == FMT_R5G6B5:
            out[..., 0], out[..., 1], out[..., 2] = _expand(v >> 11, 5), _expand((v >> 5) & 63, 6), _expand(v & 31, 5)
            out[..., 3] = 255
        elif bd.format == FMT_A1R5G5B5:
            out[..., 0], out[..., 1], out[..., 2] = _expand((v >> 10) & 31, 5), _expand((v >> 5) & 31, 5), _expand(v & 31, 5)
            out[..., 3] = np.where(v >> 15, 255, 0)
        elif bd.format == FMT_A4R4G4B4:
            out[..., 0], out[..., 1], out[..., 2] = _expand((v >> 8) & 15, 4), _expand((v >> 4) & 15, 4), _expand(v & 15, 4)
            out[..., 3] = _expand(v >> 12, 4)
        else:  # A8Y8
            out[..., 0] = out[..., 1] = out[..., 2] = px[..., 0]
            out[..., 3] = px[..., 1]
    else:
        c = px[..., 0]
        if bd.format == FMT_A8:
            out[..., :3] = 255
            out[..., 3] = c
        else:  # Y8, AY8, P8 (bump palette; greyscale preview only)
            out[..., 0] = out[..., 1] = out[..., 2] = c
            out[..., 3] = c if bd.format == FMT_AY8 else 255
    return out


def base_map_of_shader(m, shader_tag):
    """Diffuse bitmap tag of a shader, or None."""
    if shader_tag is None:
        return None
    o = m.tag_data(shader_tag)
    if shader_tag.group == "soso":
        return m.ref(o + 0xA4)
    # Other shader groups: first bitmap reference in the tag's main struct.
    for k in range(0, 0x200, 4):
        if m.u32(o + k) == 0x6269746D:
            r = m.ref(o + k)
            if r is not None:
                return r
    return None
