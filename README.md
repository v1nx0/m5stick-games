# m5stick-games

**Web flasher:** https://v1nx0.github.io/m5stick-games/ (Chrome/Edge, USB)

Games for the Flipper Zero ESP32 port on **M5StickC Plus2**, served to the
on-device **WiFi > App Store** (which also lists `trollmaster419/m5resources`).

- `games/` -- sources (one folder per app, Flipper `application.fam` format)
- `infrared/assets/*_plus.ir` -- bigger IR code packs (m5resources + Flipper-IRDB CC0), installed by
  the stick's WiFi > Update SD, not the App Store. Rebuild with `tools/merge_ir.py`.
- `apps/Games/*.fap` + `Manifest` -- built by GitHub Actions on every push to `games/`

Add a game: put its folder in `games/`, push. The workflow builds it against the
Plus2 firmware and updates `Manifest`; the App Store sees it on next open.

Controls on the Plus2: A = OK (hold = long OK), B tap = Down, B hold = Up, Power = Back.

`firmware/plus2.patch` -- App Store, Games menu and AC On changes, applied by CI to
trollmaster419/Flipper-Zero-ESP32-Port; the built firmware lands in `docs/firmware/` for the flasher.
