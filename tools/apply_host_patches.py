"""Apply patches/host/*.patch to the ACGC-PC-Port submodule (idempotent).

The host is pinned as a submodule and only ever touched through these patches,
so updating the port is: bump the submodule, re-run this, fix any rejects,
then tools/refresh_host_patch.py.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOST = ROOT / "external" / "ACGC-PC-Port"
PATCHES = sorted((ROOT / "patches" / "host").glob("*.patch"))


def git_apply(patch, *flags):
    return subprocess.run(["git", "-C", str(HOST), "apply", *flags, str(patch)],
                          capture_output=True, text=True)


def main():
    if not (HOST / "pc" / "CMakeLists.txt").exists():
        print(f"host not found at {HOST} (git submodule update --init)", file=sys.stderr)
        return 1
    for patch in PATCHES:
        if git_apply(patch, "--reverse", "--check").returncode == 0:
            print(f"   {patch.name}: already applied")
            continue
        check = git_apply(patch, "--check")
        if check.returncode != 0:
            print(f"!! {patch.name} does not apply cleanly:\n{check.stderr}", file=sys.stderr)
            return 1
        git_apply(patch)
        print(f"   {patch.name}: applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
