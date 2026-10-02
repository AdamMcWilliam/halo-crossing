# assets_local/

Your own legally obtained game data goes here. Everything in this directory
except this README is ignored by git and must never be committed.

```
assets_local/
    animal_crossing/   Animal Crossing (USA, GAFE01 Rev 0) disc image: .iso / .gcm / .ciso
    halo/generated/    written by tools/import_halo.py from your Halo CE Xbox image
    saves/             optional: an untouched copy of a town save (.gci)
```

`tools/check_disc.py` verifies that the Animal Crossing image is the supported
revision. The build scripts link `build/host/bin/rom` to
`assets_local/animal_crossing/`, so the image is never copied.

`tools/import_halo.py --iso <your Halo: Combat Evolved Xbox image>` reads the
disc in place and writes `halo/generated/`: `fp_weapons.hcpk` (the Chief's arms
and first-person weapons), `bipeds.hcpk` (Grunts, Elites and their weapons),
`sounds.hcpk` and `hud.hcpk`. The Halo image itself can stay wherever it is.
