Bad USB scripts for the M5StickC Plus2 (Flipper ESP32 port).

This board has no real USB port, so Bad USB works over BLUETOOTH:
1. Open Bad USB, pick a script.
2. On the target PC/phone, pair the stick as a Bluetooth keyboard
   (Bluetooth settings -> add device -> it shows as a keyboard).
3. Run the script; it types as that keyboard.

DuckyScript commands: STRING, ENTER, DELAY, GUI, CTRL, ALT, SHIFT, etc.
Use only on devices you own.

Payloads 01-30 are harmless demos and productivity shortcuts for Windows,
macOS, Linux, browsers, and media control. USB cable HID is not possible on
M5StickC Plus2 hardware; use the BLE keyboard connection.

Phone/iPhone and Phone/Android contain 20 additional safe keyboard shortcuts.
Pair "Keyboard New Device1", then open a payload from the matching folder.
Some shortcuts depend on the active app and phone OS version.
