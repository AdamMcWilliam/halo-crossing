#!/usr/bin/env bash
# Halo Crossing build driver. Run from an MSYS2 MINGW32 shell (or via build.bat).
#
#   ./build.sh            apply host patches, configure, build
#   ./build.sh run        build, then launch the game
#   ./build.sh test       headless Halo sandbox tests (no game data needed)
#   ./build.sh clean      delete build/host
#
# Env: HC_JOBS (parallel jobs), HC_FIRST_PERSON=1 (start in Halo camera),
#      HC_AUTOSPAWN=1 (spawn a Grunt when the Halo camera first goes live).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOST="$ROOT/external/ACGC-PC-Port"
BUILD="$ROOT/build/host"
JOBS="${HC_JOBS:-$(nproc 2>/dev/null || echo 8)}"

native_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi
}

python_cmd() {
    if command -v python3 >/dev/null 2>&1; then echo python3; else echo python; fi
}

ensure_host() {
    if [ ! -f "$HOST/pc/CMakeLists.txt" ]; then
        echo "== fetching host submodule"
        git -C "$ROOT" submodule update --init external/ACGC-PC-Port
    fi
    "$(python_cmd)" "$ROOT/tools/apply_host_patches.py"
}

link_disc() {
    local src="$ROOT/assets_local/animal_crossing" dst="$BUILD/bin/rom"
    mkdir -p "$dst"
    local found=0
    for img in "$src"/*.iso "$src"/*.gcm "$src"/*.ciso; do
        [ -e "$img" ] || continue
        found=1
        local name
        name="$(basename "$img")"
        # Hard link, so the multi-hundred-MB image is never duplicated.
        [ -e "$dst/$name" ] || ln "$img" "$dst/$name" 2>/dev/null || cp "$img" "$dst/$name"
    done
    if [ "$found" = 0 ]; then
        echo "!! no Animal Crossing disc image in assets_local/animal_crossing/ (see assets_local/README.md)"
    else
        "$(python_cmd)" "$ROOT/tools/check_disc.py" || true
    fi
}

build() {
    ensure_host
    echo "== configure"
    cmake -S "$HOST/pc" -B "$BUILD" -G Ninja -DPC_CONSOLE=ON -DHC_ROOT="$(native_path "$ROOT")"
    echo "== build ($JOBS jobs)"
    ninja -C "$BUILD" -j "$JOBS"
    link_disc
    echo "== built $BUILD/bin"
}

case "${1:-build}" in
    build) build ;;
    run)
        build
        cd "$BUILD/bin"
        exe=AnimalCrossing.exe
        [ -e "$exe" ] || exe=./AnimalCrossing
        "./$exe"
        ;;
    test) bash "$ROOT/tests/run_tests.sh" ;;
    clean) rm -rf "$BUILD" ;;
    *)
        echo "usage: $0 [build|run|test|clean]" >&2
        exit 2
        ;;
esac
