"""Sound pack (HCSD): weapon, impact, shield and Covenant dialogue sounds from
the Xbox maps' 'snd!' tags, decoded to 22050 Hz mono s16.

  header  magic 'HCSD', u32 version, u32 slot_count, u32 slots_off
  slot    u16 perm_count, u16 pad, u32 perms_off, f32 pitch_lo, f32 pitch_hi   (16 bytes)
  perm    u32 samples_off, u32 frames, f32 gain, u32 pad                       (16 bytes)
  samples s16 little-endian, mono, 22050 Hz

Slots are fixed (SOUNDS order = HcSoundId in src/integration/hc_sound.h); a
sound missing from the maps leaves its slot empty.

'snd!': +0x06 u16 sample rate (0 = 22050, 1 = 44100), +0x14 f32[2] random
pitch bounds, +0x6C u16 channels (0 mono, 1 stereo), +0x6E u16 compression
(0 PCM, 1 Xbox ADPCM), +0x98 pitch ranges (72 bytes each: +0x2C s16 playable
permutation count, +0x3C permutations). Permutation (124 bytes): +0x24 gain,
+0x2A s16 next permutation (long sounds are chained), +0x40 samples data ref.
"""
import struct

import numpy as np

MAGIC = b"HCSD"
VERSION = 1
RATE = 22050

_DIALOG = r"sound\dialog\%s\conditional\combat2" + "\\"
_W = r"sound\sfx\weapons" + "\\"
_A = _W + "weapon_anims\\"

# Per-weapon slot groups follow HaloWeaponId order:
# assault rifle, pistol, shotgun, sniper, rocket, flamethrower, plasma pistol, plasma rifle, needler, fuel rod.
FIRE = ["assault rifle\\fire", "pistol\\fire", "shotgun\\fire", "sniper rifle\\fire", "rocket launcher\\fire",
        "flamethrower\\fire", "plasma rifle\\fire", "plasma rifle\\fire", "needler\\fire", "fuel rod gun\\fire"]
READY = ["assault rifle\\weapon ready", "weapon_anims\\pistol_ready", "weapon_anims\\shotgun_ready",
         "weapon_anims\\sniper_ready", "weapon_anims\\rocket_ready", "weapon_anims\\flamethrower_ready",
         "weapon_anims\\plaspistol_ready", "weapon_anims\\plasrifle_ready", "weapon_anims\\needle_ready", None]
RELOAD = ["weapon_anims\\ar_reload", "weapon_anims\\pistol_reload", "weapon_anims\\shotgun_reload1_1",
          "weapon_anims\\sniper_reload_empty", "weapon_anims\\rocket_reload_e", "weapon_anims\\flamethrower_reload",
          None, None, "weapon_anims\\needle_reload", "weapon_anims\\rocket_reload_e"]

# Dialogue lines, in HcSoundLine order: (grunt, elite) paths under combat2\.
LINES = [
    ("alert", "groupcomm\\sighted_new_enemy", "groupcomm\\sightednewenemy"),
    ("panic", "armsflailing\\panic_run_away", None),
    ("pain", "involuntary\\pain_body_minor", "involuntary\\painminor"),
    ("pain_major", "involuntary\\pain_body_major", "involuntary\\painmajor"),
    ("death", "involuntary\\death_quiet", "involuntary\\deathquiet"),
    ("death_violent", "involuntary\\death_violent", "involuntary\\deathviolent"),
    ("death_flying", "involuntary\\death_flying", "involuntary\\deathflying"),
    ("grenade_throw", "shouting\\grenade_throwing", "shouting\\grenadethrowing"),
    ("grenade_stuck", "shouting\\grenade_danger_self", "involuntary\\screampain"),
    ("grenade_sighted", "shouting\\grenade_sighted", "shouting\\grenadesighted"),
    ("friend_died", "friendsdying\\friend_died", "friendsdying\\frienddied"),
    ("celebrate", "killingpeople\\killed_enemy_player", "killingpeople\\killedenemyplayer"),
    ("hurt_enemy", "hurtingpeople\\damaged_enemy", "hurtingpeople\\damagedenemy"),
    ("dive", "exclamations\\dive", "exclamations\\dive"),
]


def _sounds():
    out = []
    for group, paths in (("fire", FIRE), ("ready", READY), ("reload", RELOAD)):
        for i, p in enumerate(paths):
            out.append(("%s_%d" % (group, i), None if p is None else _W + p))
    out += [
        ("fire_plasma_charged", _W + "plasma rifle\\chargefire"),
        ("expl_frag", _W + "frag grenade\\expl"),
        ("expl_plasma_grenade", _W + "plasma grenade\\plasmagrenexpl"),
        ("expl_fuel_rod", _W + "fuel rod gun\\explosion"),
        ("expl_needle", _W + "needler\\expl"),
        ("expl_needle_super", r"sound\sfx\impulse\impacts\needler_super_expl"),
        ("impact_ricochet", r"sound\sfx\impulse\impacts\bullet_ricc"),
        ("impact_dirt", r"sound\sfx\impulse\impacts\dirthits"),
        ("impact_plasma", _W + "plasma rifle\\plasmahit"),
        ("impact_needle", r"sound\sfx\impulse\impacts\needle_ricc"),
        ("impact_flesh", r"sound\sfx\impulse\impacts\fleshhit"),
        ("impact_shield", _W + "shield fx\\hits"),
        ("impact_cov_shield", _W + "shield fx\\covy_shield"),
        ("cov_shield_deplete", _W + "shield fx\\deplete"),
        ("grenade_throw", _W + "frag grenade\\throwgren"),
        ("grenade_bounce", _W + "frag grenade\\bouncedirt"),
        ("grenade_stick", _W + "plasma grenade\\plasma_projectile"),
        ("overheat", _W + "plasma rifle\\overheat"),
        ("ui_shield_hit", r"sound\sfx\ui\shield_hit"),
        ("ui_shield_depleted", r"sound\sfx\ui\shield_depleted"),
        ("ui_shield_charge", r"sound\sfx\ui\shield_charge"),
        ("ui_shield_low", r"sound\sfx\ui\shield_low"),
    ]
    for who, col in (("grunt", 1), ("elite", 2)):
        for line in LINES:
            p = line[col]
            out.append(("%s_%s" % (who, line[0]), None if p is None else (_DIALOG % who) + p))
    return out


SOUNDS = _sounds()
MAPS = ("bloodgulch", "b30", "c40")   # searched in order

# Xbox ADPCM (IMA ADPCM, 36-byte blocks per channel: s16 sample, u8 step
# index, pad, then 64 nibbles low-first; 65 samples per block).
_STEPS = np.array([
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767], np.int32)
_INDEX = (-1, -1, -1, -1, 2, 4, 6, 8)


def decode_xbox_adpcm(raw, channels):
    """(frames, channels) int16."""
    block = 36 * channels
    blocks = len(raw) // block
    out = np.zeros((blocks * 65, channels), np.int16)
    steps = _STEPS.tolist()
    for b in range(blocks):
        base = b * block
        for c in range(channels):
            pred, index = struct.unpack_from("<hB", raw, base + 4 * c)
            index = min(max(index, 0), 88)
            col = out[:, c]
            o = b * 65
            col[o] = pred
            o += 1
            for chunk in range(8):
                start = base + 4 * channels + (chunk * channels + c) * 4
                for byte in raw[start:start + 4]:
                    for nib in (byte & 15, byte >> 4):
                        step = steps[index]
                        diff = step >> 3
                        if nib & 1:
                            diff += step >> 2
                        if nib & 2:
                            diff += step >> 1
                        if nib & 4:
                            diff += step
                        pred = pred - diff if nib & 8 else pred + diff
                        pred = -32768 if pred < -32768 else 32767 if pred > 32767 else pred
                        index += _INDEX[nib & 7]
                        index = 0 if index < 0 else 88 if index > 88 else index
                        col[o] = pred
                        o += 1
    return out


def _permutation_audio(m, e, channels, compression):
    size, off = m.data_ref(e + 0x40)
    raw = bytes(m.m[off:off + size])
    if compression == 1:
        pcm = decode_xbox_adpcm(raw, channels)
    elif compression == 0:
        pcm = np.frombuffer(raw[:len(raw) // (2 * channels) * 2 * channels], "<i2").reshape(-1, channels)
    else:
        return None
    return pcm


def read_sound(m, tag):
    """(list of (mono int16 at RATE, gain), pitch_lo, pitch_hi) or None."""
    o = m.tag_data(tag)
    rate = 44100 if m.unpack("H", o + 0x06)[0] == 1 else 22050
    pitch_lo, pitch_hi = m.unpack("2f", o + 0x14)
    channels = 2 if m.unpack("H", o + 0x6C)[0] == 1 else 1
    compression = m.unpack("H", o + 0x6E)[0]
    count, ranges = m.block(o + 0x98)
    if not count:
        return None
    playable = m.unpack("h", ranges + 0x2C)[0]
    pcount, perms = m.block(ranges + 0x3C)
    playable = pcount if playable <= 0 else min(playable, pcount)
    out = []
    for i in range(playable):
        parts, j, gain = [], i, m.unpack("f", perms + i * 124 + 0x24)[0]
        while 0 <= j < pcount and len(parts) < 64:
            e = perms + j * 124
            pcm = _permutation_audio(m, e, channels, compression)
            if pcm is None:
                return None
            parts.append(pcm)
            j = m.unpack("h", e + 0x2A)[0]
        pcm = np.concatenate(parts).astype(np.int32).mean(axis=1)
        if rate == 44100:
            pcm = pcm[:len(pcm) // 2 * 2].reshape(-1, 2).mean(axis=1)
        out.append((np.clip(np.round(pcm), -32768, 32767).astype("<i2"), gain if gain > 0 else 1.0))
    if not (0.25 < pitch_lo <= pitch_hi < 4.0):
        pitch_lo = pitch_hi = 1.0
    return out, pitch_lo, pitch_hi


def build_pack(load_map, log=print):
    maps = [load_map(n) for n in MAPS]
    slots = []
    total = 0
    missing = []
    for name, path in SOUNDS:
        found = None
        if path is not None:
            for m in maps:
                t = m.find("snd!", path)
                if t is not None:
                    found = read_sound(m, t)
                    if found:
                        break
        if not found:
            if path is not None:
                missing.append(name)
            slots.append(([], 1.0, 1.0))
            continue
        slots.append(found)
        total += sum(len(p) for p, _ in found[0])
    if missing:
        log("  missing: " + ", ".join(missing))

    head = struct.calcsize("<4sIII")
    slots_off = head
    perms_off = slots_off + 16 * len(slots)
    perm_count = sum(len(s[0]) for s in slots)
    samples_off = perms_off + 16 * perm_count
    slot_bytes, perm_bytes, samples = bytearray(), bytearray(), bytearray()
    pi = 0
    for perms, lo, hi in slots:
        slot_bytes += struct.pack("<HHIff", len(perms), 0, perms_off + 16 * pi if perms else 0, lo, hi)
        for pcm, gain in perms:
            perm_bytes += struct.pack("<IIfI", samples_off + len(samples), len(pcm), gain, 0)
            samples += pcm.tobytes()
            pi += 1
    data = struct.pack("<4sIII", MAGIC, VERSION, len(slots), slots_off) + slot_bytes + perm_bytes + samples
    log("  %d sounds, %d permutations, %.1f s of audio, pack %.1f MB"
        % (sum(1 for s in slots if s[0]), perm_count, total / RATE, len(data) / 1e6))
    return data
