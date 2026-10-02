#!/usr/bin/env bash
# Builds and runs the headless Halo sandbox checks with the host C compiler.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/tests"
mkdir -p "$out"
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
    -I"$root/src" -I"$root/src/halo" \
    "$root"/src/halo/*.c "$root/tests/halo_sim_test.c" -lm -o "$out/halo_sim_test"
"$out/halo_sim_test"

"$cc" -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -I"$root/src" \
    "$root/src/integration/hc_fp_pack.c" "$root/tests/fp_pack_test.c" -lm -o "$out/fp_pack_test"
"$out/fp_pack_test" "$out" "$root/assets_local/halo/generated/fp_weapons.hcpk"
