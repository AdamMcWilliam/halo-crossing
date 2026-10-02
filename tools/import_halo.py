"""Build Halo Crossing's local asset packs from your own Halo: CE Xbox disc image.

    python tools/import_halo.py --iso "D:/dumps/Halo - Combat Evolved (USA).iso"

Writes into assets_local/halo/generated/ (gitignored):
  fp_weapons.hcpk   first-person arms and weapons (from bloodgulch)
  bipeds.hcpk       Grunts, Elites and the Covenant weapons they hold (from b30, c40)
The game loads them at startup and falls back to placeholder models without them.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from halo_import import biped_pack, fp_pack  # noqa: E402
from halo_import.cache_map import load_from_disc  # noqa: E402
from halo_import.xdvdfs import XboxDisc  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS = ("fp", "bipeds")


def write(out_dir, name, data):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    print("wrote %s" % path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", required=True, help="Halo: Combat Evolved Xbox image (XISO / redump)")
    ap.add_argument("--map", default="bloodgulch", help="map to take first-person weapons from")
    ap.add_argument("--out", default=os.path.join(REPO, "assets_local", "halo", "generated"))
    ap.add_argument("--only", choices=PACKS, action="append", help="build just this pack (repeatable)")
    args = ap.parse_args()
    only = set(args.only or PACKS)

    t0 = time.time()
    with XboxDisc(args.iso) as disc:
        cache = {}

        def load_map(name):
            if name not in cache:
                print("reading maps\\%s.map" % name)
                cache[name] = load_from_disc(disc, name)
            return cache[name]

        if "fp" in only:
            print("first-person weapons:")
            data, _ = fp_pack.build_pack(load_map(args.map))
            write(args.out, "fp_weapons.hcpk", data)
        if "bipeds" in only:
            print("third-person Covenant:")
            data, _ = biped_pack.build_pack(load_map)
            write(args.out, "bipeds.hcpk", data)
    print("done in %.1f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
