# aurora_kbd_bridge Status

Last updated: 2026-02-16

## Summary

`aurora_kbd_bridge` is currently **experimental** and **not in active use** by the overlay.

Breezehand now uses direct USB access from the overlay process for the supported unsupported-dongle path.

## Current State

- USB interface discovery and endpoint acquisition in the sysmodule were partially working during development.
- Boot-style keyboard report decode (`8-byte`) was partially working.
- Shared memory allocation worked in some runs.
- **IPC service path is currently not working reliably.**

## IPC Status (Important)

IPC is currently considered broken/unreliable for this sysmodule.

Observed failures during testing included:

- `smRegisterService(...) failed rc=0xe401`
- `svcManageNamedPort(...) failed rc=0xfa01`

Because of this, the overlay could not reliably consume sysmodule state over IPC.

## Practical Impact

- Do not rely on this sysmodule for production keyboard input.
- Current recommended path is direct USB handling in overlay (`source/main.cpp`).

## If Resuming Sysmodule Work Later

1. Fix service registration / named port lifecycle first.
2. Add a minimal IPC smoke test client before keyboard decoding work.
3. Only after stable IPC, continue parser work for non-boot/vendor report formats.
