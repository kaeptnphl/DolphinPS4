# usbpad

USB game controllers for PS4 homebrew (OpenOrbis) through `sceUsbd`. Independent of Dolphin.

- C API in `include/usbpad.h`: `usbpad_init()`, `usbpad_get(slot, &info, &state)`.
- Hotplug by rescanning once per second; one reader thread per pad.
- Drivers: XInput (Xbox 360 class; 8BitDo GBros. in XInput mode). More (Switch Pro, generic HID) are
  planned; a driver is a `match()` + `parse()` pair, see `src/driver.h`.
- Read-only: nothing is written to the pad (no rumble yet).
- Build the demo app: `scripts/build-usbpad-demo.sh [upload]`; it logs to `/data/DolphinPS4/usbpad-demo.log`.

Notes from testing on the PS4:

- sceUsbd reports libusb error `-N` as `0x80240000 + N`; an idle read returns `0x80240007` (timeout).
- The device list is freed without unreferencing the devices (`unref=0`); that leaks one small
  reference per device per scan (once per second) and is a known open point.
