# Integration of kaeptnphl fork fixes into upstream v04.19

Branch: `integrate-v0419` (base: upstream `v04.19`, tag `283e246`)

This document summarizes the fork-specific fixes ported onto the upstream v04.19 release.

## USB gamepad support

- `probe-usb/`: standalone sceUsbd USB diagnostic probe
- `usbpad/`: hotplug USB gamepad library (XInput driver) with demo app
- `scripts/build-usb-probe.sh`, `scripts/build-usbpad-demo.sh`
- `patches/dolphin-ps4.patch`: usbpad integrated into the player DualShock 4 device; USB pads drive the XMB and the in-game menu (`ps4_pad_read`)
- Classic Controller and Nunchuk through a USB adapter (Wii Extension)

## Network shares (NFS / SMB)

- `patches/libnfs-ps4.patch`:
  - Fix PS4 libnfs sockaddr family handling (use `sa_family` instead of `ss_family`, which yielded `AF_FAMILY:16` on PS4)
  - Retry on EINTR in `poll()` (profiler-induced Disc-Read-Errors)
- `patches/libsmb2-ps4.patch`:
  - Retry `poll()` on `EINTR` and report `errno` on failure
- `scripts/build-netlibs.sh`: pin libnfs revision `76132364...`, preflight patch check, netlib build directory creation
- `BUILDING.md`: document network library build setup

## Build fixes

- `patches/dolphin-ps4.patch`: define `GImFontRtlReorder` locally (undefined symbol at link)

## Not included (kept from upstream v04.19)

- `xmb/game-settings.ini`, `xmb/ps4-defaults.ini` (upstream is newer)
- `port/ps4_sampler.cpp` (upstream changes)

## Build

- Version: `04.19`
- Title-ID: `DLPH00010` (Dolphin)
- Built on emma (Debian), packaged as `IV0000-DLPH00010_00-DOLPHINPS4000000.pkg`
