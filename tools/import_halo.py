"""Build Halo Crossing's local asset packs from your own Halo: CE Xbox disc image.

    python tools/import_halo.py --iso "D:/dumps/Halo - Combat Evolved (USA).iso"

Writes assets_local/halo/generated/fp_weapons.hcpk (gitignored). The game
loads it at startup and falls back to placeholder models without it.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from halo_import import fp_pack  # noqa: E402
from halo_import.cache_map import load_from_disc  # noqa: E402
from halo_import.xdvdfs import XboxDisc  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", required=True, help="Halo: Combat Evolved Xbox image (XISO / redump)")
    ap.add_argument("--map", default="bloodgulch", help="map to take first-person weapons from")
    ap.add_argument("--out", default=os.path.join(REPO, "assets_local", "halo", "generated"))
    args = ap.parse_args()

    t0 = time.time()
    with XboxDisc(args.iso) as disc:
        print("reading maps\\%s.map" % args.map)
        m = load_from_disc(disc, args.map)
    print("first-person weapons:")
    data, _ = fp_pack.build_pack(m)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "fp_weapons.hcpk")
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    print("wrote %s in %.1f s" % (path, time.time() - t0))


if __name__ == "__main__":
    main()
