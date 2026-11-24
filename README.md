# m5stick-games

Games for the Flipper Zero ESP32 port on **M5StickC Plus2**, served to the
on-device **WiFi > App Store** (which also lists `trollmaster419/m5resources`).

- `games/` -- sources (one folder per app, Flipper `application.fam` format)
- `apps/Games/*.fap` + `Manifest` -- built by GitHub Actions on every push to `games/`

Add a game: put its folder in `games/`, push. The workflow builds it against the
Plus2 firmware and updates `Manifest`; the App Store sees it on next open.

Controls on the Plus2: A = OK (hold = long OK), B tap = Down, B hold = Up, Power = Back.
