# assets_local/

Your own legally obtained game data goes here. Everything in this directory
except this README is ignored by git and must never be committed.

```
assets_local/
    animal_crossing/   Animal Crossing (USA, GAFE01 Rev 0) disc image: .iso / .gcm / .ciso
    halo/              (later) Halo CE data you extracted yourself: tags, sounds, models
```

`tools/check_disc.py` verifies that the Animal Crossing image is the supported
revision. The build scripts link `build/host/bin/rom` to
`assets_local/animal_crossing/`, so the image is never copied.
