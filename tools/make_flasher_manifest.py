"""Write docs/firmware/manifest.json for ESP Web Tools (offsets = Plus2 single-app layout)."""
import json, os, sys

parts = [("bootloader.bin", 0x1000), ("partition-table.bin", 0x8000), ("furi_esp32.bin", 0x10000)]
for name, _ in parts:
    assert os.path.getsize(f"docs/firmware/{name}") > 0, name
json.dump({
    "name": "Flipper Zero ESP32 port - M5StickC Plus2 (App Store + games + AC On)",
    "version": sys.argv[1] if len(sys.argv) > 1 else "dev",
    "new_install_prompt_erase": False,  # never offer a full-chip erase: it wipes the stick's files
    "builds": [{"chipFamily": "ESP32",
                "parts": [{"path": n, "offset": o} for n, o in parts]}],
}, open("docs/firmware/manifest.json", "w"), indent=2)
print("flasher manifest written")
