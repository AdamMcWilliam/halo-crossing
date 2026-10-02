"""HUD pack (HCHD): Halo CE's HUD from the Xbox maps' HUD interface tags
(unhi, wphi, grhi, hudg, hud#) and the bitmaps they draw.

  header   magic 'HCHD', u32 version, u32 texture_count, u32 textures_off,
           u32 element_count, u32 elements_off, u32 cell_count, u32 cells_off
  texture  u16 width, u16 height (multiples of 4), u32 pixels_off       (8 bytes)
           pixels: GameCube RGBA8 tiles, 32-byte aligned
  element  u8 kind, u8 group, u8 state, u8 anchor, s16 x, s16 y, s16 w, s16 h,
           u16 texture, u16 first_cell, u16 cell_count, u16 flags, u32 colour[5]  (40 bytes)
  cell     u16 texture, s16 x, s16 y, u16 columns, u32 columns_off, u32 pad  (16 bytes)

Coordinates are CE's 640x480 HUD space. (x, y) is the sprite's top-left
relative to its anchor corner (0 top-left, 1 top-right, 2 bottom-left,
3 bottom-right, 4 screen centre). Colours are 0xRRGGBBAA.

Meters are split into cells by the order the bitmap's gradient fills them
in: tick meters (ammo, health segments) get one cell per gradient level,
sorted so the first N light for N of the maximum; smooth bars (shields,
heat, battery) are one cell with a per-column fill level (0-254, 255 = no
pixels) that the game cuts into lit and unlit runs.
"""
import struct

import numpy as np

from . import bitmaps, textures

MAGIC = b"HCHD"
VERSION = 1

(K_STATIC, K_METER, K_NUMBER, K_RETICLE, K_WARNING, K_DIGIT, K_BLIP, K_SCOPE, K_DAMAGE,
 K_SENSOR, K_CUTOFFS) = range(11)
G_UNIT, G_WEAPON, G_GRENADE, G_GLOBAL = 0, 1, 11, 13       # weapon k -> G_WEAPON + k, grenade t -> G_GRENADE + t
S_SHIELD, S_HEALTH, S_SENSOR_BG, S_SENSOR_FG = 0, 1, 2, 3  # unit states; weapon states are CE's (0 total
S_GREN_BG, S_GREN_COUNT_BG, S_GREN_NUMBER, S_GREN_ICON, S_GREN_ICON_EMPTY = 0, 1, 2, 3, 4  # ammo .. 3 age)
METER_CONTINUOUS = 0x100
ANCHOR_CENTRE = 4

MAPS = ("bloodgulch", "c20", "d20")
UNIT_HUD = r"ui\hud\cyborg"
# HaloWeaponId order; the fuel rod has no HUD tags on the Xbox discs.
WEAPON_HUDS = [r"weapons\assault rifle\assault rifle", r"weapons\pistol\pistol", r"weapons\shotgun\shotgun",
               r"weapons\sniper rifle\sniper rifle", r"weapons\rocket launcher\rocket_launcher",
               r"weapons\flamethrower\flame thrower", r"weapons\plasma pistol\plasma pistol",
               r"weapons\plasma rifle\plasma rifle", r"weapons\needler\needler", None]
GRENADE_HUDS = [r"ui\hud\default", r"ui\hud\plasma_flare"]
WARNING_TYPES = (3, 6, 8, 9, 10, 18)   # reload, low battery, no ammo, no grenades, low ammo, battery depleted
SCOPES = [r"ui\hud\bitmaps\sniper\sniper_scope_mask2", r"ui\hud\bitmaps\pistol\pistol_scope_mask"]
BLIP = r"ui\hud\bitmaps\hud_sensor_blip"
MAX_TICK_LEVELS = 64
WARNING_RGBA = 0xAAD5FFFF
METER_ALPHA, EMPTY_ALPHA = 0xE6, 0x99
OVERLAY_NOT_A_SPRITE, OVERLAY_ONLY_ZOOMED = 0x2, 0x4

ELEMENT = struct.Struct("<BBBBhhhhHHHH5I")
CELL = struct.Struct("<HhhHII")


def _colour(argb):
    """Tag colour (u32 0xAARRGGBB) -> 0xRRGGBBAA."""
    return ((argb & 0xFFFFFF) << 8) | (argb >> 24)


class _Sheet:
    """A bitm tag's decoded bitmaps and its sequences' sprite rectangles."""

    def __init__(self, m, tag):
        o = m.tag_data(tag)
        self.images = [bitmaps.decode(m, bd) for bd in bitmaps.read_bitmap_entries(m, tag)]
        self.sequences = []   # per sequence: [(bitmap index, (left, right, top, bottom) or None)]
        n, seq = m.block(o + 0x54)
        for i in range(n):
            s = seq + i * 64
            first = m.unpack("h", s + 0x20)[0]
            sn, sp = m.block(s + 0x34)
            if sn:
                self.sequences.append([(m.unpack("h", sp + k * 32)[0], m.unpack("4f", sp + k * 32 + 8))
                                       for k in range(sn)])
            else:
                self.sequences.append([(first, None)])

    def sprite(self, seq, index=0):
        if not 0 <= seq < len(self.sequences) or index >= len(self.sequences[seq]):
            return None
        bi, rect = self.sequences[seq][index]
        img = self.images[bi] if 0 <= bi < len(self.images) else None
        if img is None or rect is None:
            return img
        h, w = img.shape[:2]
        l, r, t, b = rect
        return img[int(round(t * h)):int(round(b * h)), int(round(l * w)):int(round(r * w))]


class _Builder:
    def __init__(self, maps, log):
        self.maps, self.log = maps, log
        self.textures, self.elements, self.cells, self.columns = [], [], [], bytearray()
        self._sheets = {}

    # -- sources ------------------------------------------------------------
    def find(self, group, path):
        for m in self.maps:
            t = m.find(group, path)
            if t is not None:
                return m, t
        return None, None

    def sheet(self, m, tag):
        key = (m.name, tag.path)
        if key not in self._sheets:
            self._sheets[key] = _Sheet(m, tag)
        return self._sheets[key]

    def sprite_at(self, m, ref_off, seq):
        ref = m.ref(ref_off)
        if ref is None:
            return None
        img = self.sheet(m, ref).sprite(seq)
        return None if img is None or img.size == 0 else img

    # -- output -------------------------------------------------------------
    def texture(self, img):
        h, w = img.shape[:2]
        pw, ph = -w % 4, -h % 4
        if pw or ph:
            img = np.pad(img, ((0, ph), (0, pw), (0, 0)))
        self.textures.append((img.shape[1], img.shape[0], textures.rgba_to_gc_rgba8(np.ascontiguousarray(img))))
        return len(self.textures) - 1

    def element(self, kind, group, state, anchor, x, y, w, h, tex=0xFFFF, cells=(0, 0), flags=0, colours=()):
        c = list(colours) + [0] * (5 - len(colours))
        self.elements.append((kind, group, state, anchor, int(x), int(y), int(w), int(h), tex, cells[0], cells[1],
                              flags, *c))

    @staticmethod
    def corner_offset(anchor, off, size):
        """Top-left of a sprite relative to its anchor corner, from CE's anchor offset."""
        (ox, oy), (w, h) = off, size
        x = ox if anchor in (0, 2) else -ox - w
        y = oy if anchor in (0, 1) else -oy - h
        return x, y

    def static(self, m, base, group, state, anchor, seq_off=0x54):
        """A placed bitmap (CE 'hud background' / static element) at `base` (anchor offset)."""
        colour = m.unpack("I", base + 0x34)[0]
        if colour >> 24 == 0:
            return False
        img = self.sprite_at(m, base + 0x24, m.unpack("h", base + seq_off)[0])
        if img is None:
            return False
        img = _crop(img)
        if img is None:
            return False
        img, (cx, cy) = img
        full = self._full_size(m, base, seq_off)
        x, y = self.corner_offset(anchor, m.unpack("hh", base), full)
        self.element(K_STATIC, group, state, anchor, x + cx, y + cy, img.shape[1], img.shape[0], self.texture(img),
                     colours=(_colour(colour), _colour(m.unpack("I", base + 0x38)[0])))
        return True

    def _full_size(self, m, base, seq_off):
        img = self.sprite_at(m, base + 0x24, m.unpack("h", base + seq_off)[0])
        return img.shape[1], img.shape[0]

    def meter(self, m, base, group, state, anchor, extra=()):
        """CE meter: colours at +0x34 (min, max, flash, empty), min value +0x45, sequence +0x46."""
        img = self.sprite_at(m, base + 0x24, m.unpack("h", base + 0x46)[0])
        if img is None:
            return False
        cmin, cmax, cflash, cempty = m.unpack("4I", base + 0x34)
        minv = m.unpack("B", base + 0x45)[0]
        x, y = self.corner_offset(anchor, m.unpack("hh", base), (img.shape[1], img.shape[0]))
        first = len(self.cells)
        alpha = img[..., 3]
        level = img[..., 0]
        shape = alpha > 0
        if not shape.any():
            return False
        solid = level[alpha >= 128] if (alpha >= 128).any() else level[shape]
        levels = _levels(solid)
        lo, hi = int(levels.min()), int(levels.max())
        flags = minv
        if len(np.unique(solid)) <= MAX_TICK_LEVELS:
            # Every pixel joins the nearest solid level (anti-aliased edges included).
            nearest = levels[np.abs(level[..., None].astype(int) - levels[None, None, :].astype(int)).argmin(axis=2)]
            for lv in levels:
                mask = shape & (nearest == lv)
                ys, xs = np.nonzero(mask)
                if not len(xs):
                    continue
                x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
                cell = np.zeros((y1 - y0, x1 - x0, 4), np.uint8)
                cell[..., :3] = 255
                cell[..., 3] = np.where(mask[y0:y1, x0:x1], alpha[y0:y1, x0:x1], 0)
                self.cells.append((self.texture(cell), int(x0), int(y0), 0, 0))
        else:
            flags |= METER_CONTINUOUS
            lo, hi = int(solid.min()), int(solid.max())
            cols = np.full(img.shape[1], 255, np.uint8)
            for cx in range(img.shape[1]):
                sel = shape[:, cx]
                if sel.any():
                    v = float(np.median(level[sel, cx]))
                    cols[cx] = int(round(min(max((v - lo) / max(1, hi - lo), 0.0), 1.0) * 254))
            cell = np.zeros(img.shape, np.uint8)
            cell[..., :3] = 255
            cell[..., 3] = alpha
            off = len(self.columns)
            self.columns += cols.tobytes()
            self.cells.append((self.texture(cell), 0, 0, img.shape[1], off))
        # Meter colours carry no alpha (CE's opacity comes from elsewhere); an all-black empty colour is not drawn.
        colours = [_colour(c) | METER_ALPHA for c in (cmin, cmax)]
        colours += [(_colour(cempty) | EMPTY_ALPHA) if cempty & 0xFFFFFF else 0, _colour(cflash) | METER_ALPHA]
        colours += [_colour(c) | METER_ALPHA for c in extra]
        self.element(K_METER, group, state, anchor, x, y, img.shape[1], img.shape[0],
                     cells=(first, len(self.cells) - first), flags=flags, colours=colours)
        return True

    def number(self, m, base, colour_off, digits_off, group, state, anchor):
        digits, nflags = m.unpack("BB", base + digits_off)
        x, y = m.unpack("hh", base)
        colour, flash = m.unpack("2I", base + colour_off)
        if anchor not in (0, 2):
            x = -x
        if anchor not in (0, 1):
            y = -y
        self.element(K_NUMBER, group, state, anchor, x, y, 0, 0, flags=digits | (nflags << 8),
                     colours=(_colour(colour), _colour(flash)))

    def crosshair_sprite(self, m, ch, ov, kind, group, state):
        img = self.sprite_at(m, ch + 0x24, m.unpack("h", ov + 0x46)[0])
        if img is None:
            return False
        colour = m.unpack("I", ov + 0x24)[0]
        ox, oy = m.unpack("hh", ov)
        h, w = img.shape[:2]
        if kind == K_RETICLE:
            x, y = ox - w // 2, oy - h // 2
        else:
            x, y = -(w // 2), oy
        # Crosshair colours leave alpha at 0 and warnings leave the colour black too: CE draws both opaque,
        # warnings in the HUD's outline blue.
        rgba = (_colour(colour) | 0xFF) if colour & 0xFFFFFF else WARNING_RGBA
        self.element(kind, group, state, ANCHOR_CENTRE, x, y, w, h, self.texture(img), colours=(rgba,))
        return True


def _levels(values):
    """Distinct fill levels of a meter: values held by a handful of stray pixels are dropped and values
    within 3 of each other merged (bitmap noise)."""
    found, counts = np.unique(values, return_counts=True)
    keep = counts >= max(2, 0.25 * float(np.median(counts)))
    found, counts = found[keep], counts[keep]
    groups = []
    for v, c in zip(found.tolist(), counts.tolist()):
        if groups and v - groups[-1][0][0] <= 3:
            groups[-1].append((v, c))
        else:
            groups.append([(v, c)])
    return np.array([max(g, key=lambda vc: vc[1])[0] for g in groups], np.uint8)


def _crop(img):
    ys, xs = np.nonzero(img[..., 3] > 0)
    if not len(xs):
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return img[y0:y1, x0:x1], (int(x0), int(y0))


def _unit(b):
    m, t = b.find("unhi", UNIT_HUD)
    if t is None:
        b.log("  missing: unit HUD")
        return
    o = m.tag_data(t)
    anchor = m.unpack("H", o)[0]
    b.static(m, o + 0x8C, G_UNIT, S_SHIELD, anchor)
    b.meter(m, o + 0xF4, G_UNIT, S_SHIELD, anchor)
    b.static(m, o + 0x17C, G_UNIT, S_HEALTH, anchor)
    b.meter(m, o + 0x1E4, G_UNIT, S_HEALTH, anchor, extra=(m.unpack("I", o + 0x1E4 + 0x68)[0],))
    # The motion sensor sits in the bottom-left corner whatever the HUD's anchor.
    b.static(m, o + 0x26C, G_UNIT, S_SENSOR_BG, 2)
    b.static(m, o + 0x2D4, G_UNIT, S_SENSOR_FG, 2)
    cx, cy = m.unpack("hh", o + 0x35C)
    bg = b.sprite_at(m, o + 0x26C + 0x24, m.unpack("h", o + 0x26C + 0x54)[0])
    radius = 0
    if bg is not None:
        ys, xs = np.nonzero(bg[..., 3] >= 128)
        radius = int(round((xs.max() - xs.min() + 1) / 2)) if len(xs) else 0
    b.element(K_SENSOR, G_UNIT, 0, 2, cx, -cy, radius, radius)


def _weapon(b, k, path):
    m, t = b.find("wphi", path)
    if t is None:
        return False
    group = G_WEAPON + k
    o = m.tag_data(t)
    total, loaded, heat, age = m.unpack("4h", o + 0x14)
    b.element(K_CUTOFFS, group, 0, 0, total, loaded, heat, age)
    reticle = False
    warned = set()
    chain, seen = [], set()
    while t is not None and t.path not in seen:
        seen.add(t.path)
        chain.append(t)
        t = m.ref(m.tag_data(t))
    for t in chain:
        o = m.tag_data(t)
        anchor = m.unpack("H", o + 0x3C)[0]
        n, p = m.block(o + 0x60)
        for i in range(n):
            e = p + i * 180
            state, maptype = m.unpack("H", e)[0], m.unpack("H", e + 4)[0]
            if maptype in (0, 1):
                b.static(m, e + 0x24, group, state, anchor)
        n, p = m.block(o + 0x6C)
        for i in range(n):
            e = p + i * 180
            state, maptype = m.unpack("H", e)[0], m.unpack("H", e + 4)[0]
            if maptype in (0, 1):
                b.meter(m, e + 0x24, group, state, anchor)
        n, p = m.block(o + 0x78)
        for i in range(n):
            e = p + i * 160
            state, maptype = m.unpack("H", e)[0], m.unpack("H", e + 4)[0]
            if maptype in (0, 1) and state in (0, 1, 2, 3):
                b.number(m, e + 0x24, 0x24, 0x44, group, state, anchor)
        n, p = m.block(o + 0x84)
        for i in range(n):
            ch = p + i * 104
            ctype, maptype = m.unpack("H", ch)[0], m.unpack("H", ch + 4)[0]
            on, op = m.block(ch + 0x34)
            if not on or maptype not in (0, 1):
                continue
            if ctype == 0 and not reticle:
                if not m.unpack("I", op + 0x48)[0] & (OVERLAY_NOT_A_SPRITE | OVERLAY_ONLY_ZOOMED):
                    reticle = b.crosshair_sprite(m, ch, op, K_RETICLE, group, 0)
            elif ctype in WARNING_TYPES and ctype not in warned:
                if b.crosshair_sprite(m, ch, op, K_WARNING, group, ctype):
                    warned.add(ctype)
    return True


def _grenade(b, k, path):
    m, t = b.find("grhi", path)
    if t is None:
        return False
    group = G_GRENADE + k
    o = m.tag_data(t)
    anchor = m.unpack("H", o)[0]
    b.static(m, o + 0x24, group, S_GREN_BG, anchor)
    b.static(m, o + 0x8C, group, S_GREN_COUNT_BG, anchor)
    b.number(m, o + 0xF4, 0x24, 0x44, group, S_GREN_NUMBER, anchor)
    icons = m.ref(o + 0x14C)
    n, p = m.block(o + 0x15C)
    for i in range(n if icons else 0):
        ov = p + i * 136
        seq, kind = m.unpack("hH", ov + 0x48)
        img = b.sheet(m, icons).sprite(seq)
        if img is None or img.size == 0:
            continue
        x, y = b.corner_offset(anchor, m.unpack("hh", ov), (img.shape[1], img.shape[0]))
        state = S_GREN_ICON if kind & 4 else S_GREN_ICON_EMPTY
        b.element(K_STATIC, group, state, anchor, x, y, img.shape[1], img.shape[0], b.texture(img),
                  colours=(_colour(m.unpack("I", ov + 0x24)[0]), _colour(m.unpack("I", ov + 0x28)[0])))
    return True


def _globals(b):
    m, t = b.find("hud#", r"ui\hud\counter")
    if t is not None and m.ref(m.tag_data(t)) is not None:
        o = m.tag_data(t)
        sheet = b.sheet(m, m.ref(o))
        # Sprites 0-9 are the digits, then '.', ':', '-', 'm'.
        advance, xoff, yoff, dot = m.unpack("BbbB", o + 0x11)
        for i in range(14):
            d = sheet.sprite(0, i)
            if d is not None and d.size:
                b.element(K_DIGIT, G_GLOBAL, i, 0, xoff, yoff, d.shape[1], d.shape[0], b.texture(d),
                          flags=advance | (dot << 8))
    m, t = b.find("hudg", r"ui\hud\default")
    if t is not None:
        o = m.tag_data(t)
        margins = m.unpack("4h", o + 0x310)
        img = b.sprite_at(m, o + 0x338, m.unpack("h", o + 0x348)[0])
        colour = _colour(m.unpack("I", o + 0x34C)[0])
        if img is not None:
            # Chevrons pointing in from the screen edge a hit came from: ahead, right, behind, left.
            for d, rot in enumerate((img, np.rot90(img, -1), img[::-1], np.rot90(img, 1))):
                rot = np.ascontiguousarray(rot)
                b.element(K_DAMAGE, G_GLOBAL, d, ANCHOR_CENTRE, margins[d], 0, rot.shape[1], rot.shape[0],
                          b.texture(rot), colours=(colour,))
    for k, path in enumerate(SCOPES):
        m, t = b.find("bitm", path)
        if t is not None:
            img = b.sheet(m, t).sprite(0)
            if img is not None:
                img = textures.downscale(img, 256)
                mask = np.zeros(img.shape, np.uint8)
                mask[..., 3] = img[..., 3]
                b.element(K_SCOPE, G_GLOBAL, k, 0, 0, 0, 640, 480, b.texture(mask), colours=(0x000000FF,))
    m, t = b.find("bitm", BLIP)
    if t is not None:
        img = b.sheet(m, t).sprite(0)
        if img is not None:
            b.element(K_BLIP, G_GLOBAL, 0, 0, 0, 0, img.shape[1], img.shape[0], b.texture(img))


def build_pack(load_map, log=print):
    maps = [load_map(n) for n in MAPS]
    b = _Builder(maps, log)
    _unit(b)
    missing = []
    for k, path in enumerate(WEAPON_HUDS):
        if path is None or not _weapon(b, k, path):
            missing.append("weapon %d" % k)
    for k, path in enumerate(GRENADE_HUDS):
        if not _grenade(b, k, path):
            missing.append("grenade %d" % k)
    _globals(b)
    if missing:
        log("  no HUD tags for: " + ", ".join(missing))

    head = 32
    tex_table = head
    elem_off = tex_table + 8 * len(b.textures)
    cell_off = elem_off + ELEMENT.size * len(b.elements)
    col_off = cell_off + CELL.size * len(b.cells)
    out = bytearray(col_off + len(b.columns))
    out[col_off:] = b.columns
    pixels = []
    for w, h, data in b.textures:
        out.extend(b"\0" * (-len(out) % 32))
        pixels.append(len(out))
        out.extend(data)
    struct.pack_into("<4s7I", out, 0, MAGIC, VERSION, len(b.textures), tex_table, len(b.elements), elem_off,
                     len(b.cells), cell_off)
    for i, (w, h, _) in enumerate(b.textures):
        struct.pack_into("<HHI", out, tex_table + 8 * i, w, h, pixels[i])
    for i, e in enumerate(b.elements):
        ELEMENT.pack_into(out, elem_off + ELEMENT.size * i, *e)
    for i, (tex, x, y, ncols, coff) in enumerate(b.cells):
        CELL.pack_into(out, cell_off + CELL.size * i, tex, x, y, ncols, col_off + coff if ncols else 0, 0)
    kinds = {}
    for e in b.elements:
        kinds[e[0]] = kinds.get(e[0], 0) + 1
    log("  %d elements (%d meters, %d cells), %d textures, pack %.2f MB"
        % (len(b.elements), kinds.get(K_METER, 0), len(b.cells), len(b.textures), len(out) / 1e6))
    return bytes(out)
