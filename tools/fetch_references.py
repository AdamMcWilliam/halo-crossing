"""Clone the Halo reverse-engineering projects used as research references
into external/ref/ (git-ignored, read-only). Nothing from them is built or
redistributed; halo_tuning.c cites which values came from where."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REF = ROOT / "external" / "ref"
REPOS = [
    # name, url, pinned commit
    ("halocea", "https://github.com/surreptitiousresearch/halocea.git", "570c83fd"),
    ("halo-re", "https://github.com/halo-re/halo.git", "21bc641"),
    ("halo-punpckhdq", "https://github.com/punpckhdq/halo.git", "901aee1"),
]


def run(*args):
    subprocess.run(args, check=True)


def main():
    REF.mkdir(parents=True, exist_ok=True)
    for name, url, commit in REPOS:
        dst = REF / name
        if not dst.exists():
            run("git", "clone", "--filter=blob:none", url, str(dst))
        run("git", "-C", str(dst), "fetch", "--quiet", "origin")
        run("git", "-C", str(dst), "checkout", "--quiet", commit)
        print(f"   {name} @ {commit}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
