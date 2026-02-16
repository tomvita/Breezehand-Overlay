# Direct USB Dongle Guide (No Sysmodule)

This guide explains how Breezehand accesses unsupported USB keyboard dongles directly from the overlay, and how to adapt support for other dongles.

## Goal

Use direct `usb:hs` access in the overlay when a dongle is not usable through normal Switch HID keyboard APIs.

## Where the code is

- `source/main.cpp`
  - Direct USB state and polling:
    - `DirectUsbInputState`
    - `tryAcquireDirectUsbLinks(...)`
    - `pollDirectUsbKeyboard(...)`
    - `decodeBootReport(...)`
  - Keyboard integration:
    - `KeyboardGui::handlePhysicalKeyboardInput()`

## Current flow

1. Overlay initializes HID keyboard support (`hidInitializeKeyboard`, `hidEnableUsbFullKeyController`).
2. In `KeyboardGui::handlePhysicalKeyboardInput()`, Breezehand also polls direct USB.
3. Direct USB path:
   - `usbHsInitialize()`
   - query interfaces with `usbHsQueryAvailableInterfaces(...)`
   - acquire interface with `usbHsAcquireUsbIf(...)`
   - pick interrupt IN endpoint (prefer 8-byte, endpoint `0x81`)
   - open endpoint and poll async transfer reports
4. Boot report decoder emits key down/up events.
5. Key down events are mapped to GUI actions/characters.

## How to add support for another dongle

### 1. Set VID/PID filter

In `source/main.cpp`, update:

- `kAuroraVid`
- `kAuroraPid`

to your dongle's vendor/product IDs.

### 2. Verify endpoint/interface selection

Check scoring and endpoint pick logic:

- `scoreInterfaceEndpoint(...)`
- `pickInputEndpoint(...)`

If your dongle uses a different endpoint pattern, adjust these heuristics.

### 3. Decide report format

Current decoder expects HID boot keyboard report (8 bytes):

- byte 0: modifiers
- byte 2..7: up to 6 key usages

If your dongle sends non-boot/NKRO/vendor reports, add another decoder:

1. detect the report shape in `pollDirectUsbLink(...)`
2. parse to usages/modifiers
3. emit events via `pushDirectUsbEvent(...)`

### 4. Keep key transitions correct

Input reliability depends on correct transition logic:

- emit `down` only when key changes up->down
- emit `up` when key disappears from report
- track modifier transitions separately

### 5. Map usages to characters/actions

Character/action mapping is handled by:

- `mapKeyboardUsageToChar(...)`
- `handleUsageDown(...)`

Add missing usage mappings there as needed.

## Debugging

Compile-time logging is available but disabled by default:

- `DIRECT_USB_LOGGING` in `source/main.cpp`
- set to `1` to enable
- set to `0` for release (logging code excluded)

When enabled, log path:

- `sdmc:/config/breezehand/overlay_usb_kbd.log`

Useful things to check:

- `usbHsInitialize` success/failure
- interface discovery success
- endpoint open success
- key `down/up` event lines

## Common failure cases

- Wrong VID/PID filter: no interface found.
- Non-boot report format: endpoint receives data but no decoded key events.
- Endpoint heuristic mismatch: acquired wrong interface/endpoint.
- Dongle requires extra HID control setup before reports become meaningful.

## Practical bring-up checklist

1. Confirm VID/PID from PC USB descriptor dump.
2. Set filter IDs in `source/main.cpp`.
3. Enable `DIRECT_USB_LOGGING=1`.
4. Open KeyboardGui and type.
5. Check log for:
   - interface acquired
   - endpoint opened
   - key down/up events
6. If no events, add/adjust report decoder for your dongle format.
7. Disable logging (`DIRECT_USB_LOGGING=0`) once stable.
