"""Check the user's Animal Crossing disc image in assets_local/animal_crossing/.

The host port targets the USA release (GAFE01). Only the disc header is read;
nothing is extracted or copied.
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DISC_DIR = ROOT / "assets_local" / "animal_crossing"
GC_MAGIC = 0xC2339F3D
SUPPORTED = {"GAFE01"}


def read_header(path):
    with open(path, "rb") as f:
        head = f.read(0x8000 + 0x40)
    if head[:4] == b"CISO":
        head = head[0x8000:]  # CISO data starts after its block map
    game_id = head[:6].decode("ascii", "replace")
    version = head[7]
    magic = struct.unpack(">I", head[0x1C:0x20])[0]
    title = head[0x20:0x60].split(b"\0", 1)[0].decode("ascii", "replace")
    return game_id, version, magic, title


def main():
    images = [p for p in sorted(DISC_DIR.glob("*")) if p.suffix.lower() in (".iso", ".gcm", ".ciso")]
    if not images:
        print(f"no disc image in {DISC_DIR}", file=sys.stderr)
        return 1
    ok = False
    for img in images:
        game_id, version, magic, title = read_header(img)
        good = magic == GC_MAGIC and game_id in SUPPORTED
        ok |= good
        state = "ok" if good else "UNSUPPORTED"
        print(f"   {img.name}: {game_id} rev {version} '{title}' -> {state}")
        if magic != GC_MAGIC:
            print("     not a GameCube disc image (bad magic)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
