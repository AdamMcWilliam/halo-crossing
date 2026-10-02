"""Texture conversion: Xbox bitmap pixel data -> RGBA and GameCube texture formats.

GameCube layouts match the port's decoders in pc/src/pc_gx_texture.c:
  CMPR   8x8 tiles of four DXT1 blocks (TL, TR, BL, BR), colours big-endian,
         2-bit indices most-significant pixel first.
  RGBA8  4x4 tiles, 32 bytes of A,R pairs then 32 bytes of G,B pairs.
  RGB5A3 4x4 tiles of big-endian u16: 1RRRRRGGGGGBBBBB or 0AAARRRRGGGGBBBB.
"""
import numpy as np


def _565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], axis=-1).astype(np.int32)


def _dxt_colour_blocks(blocks, one_bit_alpha):
    """blocks: (N, 8) uint8 DXT colour blocks (little-endian). -> (N, 16, 4) RGBA."""
    c0 = blocks[:, 0].astype(np.int32) | (blocks[:, 1].astype(np.int32) << 8)
    c1 = blocks[:, 2].astype(np.int32) | (blocks[:, 3].astype(np.int32) << 8)
    p0, p1 = _565(c0), _565(c1)
    four = (c0 > c1) | (not one_bit_alpha)
    p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], axis=1)  # (N, 4, 3)
    alpha = np.full((len(blocks), 4), 255, np.int32)
    if one_bit_alpha:
        alpha[:, 3] = np.where(four, 255, 0)
    bits = blocks[:, 4:8].astype(np.uint32)
    word = bits[:, 0] | (bits[:, 1] << 8) | (bits[:, 2] << 16) | (bits[:, 3] << 24)
    idx = (word[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3  # (N, 16)
    rgb = np.take_along_axis(pal, idx[:, :, None].astype(np.int64).repeat(3, 2), axis=1)
    a = np.take_along_axis(alpha, idx.astype(np.int64), axis=1)
    return np.concatenate([rgb, a[:, :, None]], axis=2)


def _blocks_to_image(px, w, h):
    """(N, 16, 4) block pixels in raster block order -> (h, w, 4) uint8."""
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w].astype(np.uint8)


def decode_dxt(data, w, h, kind):
    """kind: 'dxt1', 'dxt3' or 'dxt5'. Returns (h, w, 4) uint8 RGBA."""
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    n = bw * bh
    if kind == "dxt1":
        blocks = np.frombuffer(data, np.uint8, n * 8).reshape(n, 8)
        return _blocks_to_image(_dxt_colour_blocks(blocks, True), w, h)
    raw = np.frombuffer(data, np.uint8, n * 16).reshape(n, 16)
    px = _dxt_colour_blocks(raw[:, 8:16], False)
    if kind == "dxt3":
        a4 = raw[:, :8]
        nib = np.stack([a4 & 15, a4 >> 4], axis=-1).reshape(n, 16).astype(np.int32)
        px[:, :, 3] = nib * 17
    else:
        a0 = raw[:, 0].astype(np.int32)
        a1 = raw[:, 1].astype(np.int32)
        k = np.arange(8)
        interp8 = ((7 - k[None, 1:7]) * a0[:, None] + k[None, 1:7] * a1[:, None]) // 7
        interp6 = ((5 - k[None, 1:5]) * a0[:, None] + k[None, 1:5] * a1[:, None]) // 5
        pal = np.zeros((n, 8), np.int32)
        pal[:, 0], pal[:, 1] = a0, a1
        big = a0 > a1
        pal[:, 2:8] = np.where(big[:, None], interp8, np.concatenate(
            [interp6, np.zeros((n, 1), np.int32), np.full((n, 1), 255, np.int32)], axis=1))
        bits = np.zeros(n, np.uint64)
        for i in range(6):
            bits |= raw[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
        idx = (bits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)
        px[:, :, 3] = np.take_along_axis(pal, idx.astype(np.int64), axis=1)
    return _blocks_to_image(px, w, h)


def unswizzle(data, w, h, bpp):
    """Xbox linear-swizzled (Morton order) texture -> row-major bytes."""
    xs, ys = np.meshgrid(np.arange(w, dtype=np.int64), np.arange(h, dtype=np.int64))
    # Interleave x and y bits while both have bits left; the larger axis carries on alone.
    idx = np.zeros_like(xs)
    out_bit = 0
    for b in range(16):
        if (1 << b) < w:
            idx |= ((xs >> b) & 1) << out_bit
            out_bit += 1
        if (1 << b) < h:
            idx |= ((ys >> b) & 1) << out_bit
            out_bit += 1
    src = np.frombuffer(data, np.uint8, w * h * bpp).reshape(-1, bpp)
    return src[idx.reshape(-1)].reshape(h, w, bpp)


def dxt1_to_cmpr(data, w, h):
    """Lossless DXT1 -> GameCube CMPR. w, h must be multiples of 8."""
    bw, bh = w // 4, h // 4
    blocks = np.frombuffer(data, np.uint8, bw * bh * 8).reshape(bh, bw, 8).copy()
    # Endpoints to big-endian.
    blocks[:, :, [0, 1, 2, 3]] = blocks[:, :, [1, 0, 3, 2]]
    # Index bytes: reverse the four 2-bit fields in each row byte.
    rows = blocks[:, :, 4:8]
    rows = ((rows & 0x03) << 6) | ((rows & 0x0C) << 2) | ((rows & 0x30) >> 2) | ((rows & 0xC0) >> 6)
    blocks[:, :, 4:8] = rows
    tiles = blocks.reshape(bh // 2, 2, bw // 2, 2, 8).transpose(0, 2, 1, 3, 4)
    return tiles.reshape(-1).tobytes()


def _tiles4(img):
    h, w, c = img.shape
    return img.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def rgba_to_gc_rgba8(img):
    t = _tiles4(img)
    ar = t[:, :, [3, 0]].reshape(-1, 32)
    gb = t[:, :, [1, 2]].reshape(-1, 32)
    return np.concatenate([ar, gb], axis=1).astype(np.uint8).tobytes()


def rgba_to_gc_rgb5a3(img):
    t = _tiles4(img).astype(np.uint32)
    r, g, b, a = t[..., 0], t[..., 1], t[..., 2], t[..., 3]
    opaque = (1 << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    trans = ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)
    v = np.where(a >= 0xE0, opaque, trans).astype(">u2")
    return v.tobytes()


def downscale(img, max_dim):
    """Box-filter halve until both sides are <= max_dim."""
    while max(img.shape[0], img.shape[1]) > max_dim and min(img.shape[0], img.shape[1]) >= 16:
        h, w = img.shape[0] // 2 * 2, img.shape[1] // 2 * 2
        f = img[:h, :w].astype(np.uint16)
        img = ((f[0::2, 0::2] + f[1::2, 0::2] + f[0::2, 1::2] + f[1::2, 1::2] + 2) // 4).astype(np.uint8)
    return img
