"""Regenerate patches/host/0001-halo-crossing-hooks.patch from the submodule's
working tree (tracked files only). Run after editing hook call sites in the host."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOST = ROOT / "external" / "ACGC-PC-Port"
OUT = ROOT / "patches" / "host" / "0001-halo-crossing-hooks.patch"


def main():
    diff = subprocess.run(["git", "-C", str(HOST), "diff", "--no-color", "--no-ext-diff"],
                          capture_output=True, text=True, check=True).stdout
    if not diff.strip():
        print("host has no local changes; patch left untouched", file=sys.stderr)
        return 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(diff, encoding="utf-8", newline="\n")
    files = [l[6:] for l in diff.splitlines() if l.startswith("+++ b/")]
    print(f"wrote {OUT.relative_to(ROOT)} ({len(files)} files: {', '.join(files)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
