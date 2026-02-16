# aurora_kbd_bridge (WIP)

See current development state:

- `sysmodules/aurora_kbd_bridge/STATUS.md`

This is a first-pass Atmosphere sysmodule for USB keyboard bridging of
`VID:PID 1A2C:8FFF` (Aurora/Lenovo GK10 receiver family).

It uses `usb:hs` directly (not `hidGetKeyboardStates`) to:

1. Find HID keyboard interfaces for the target VID/PID.
2. Acquire one interrupt IN endpoint.
3. Poll reports and decode boot-style 8-byte keyboard data.
4. Export current state via shared memory service:
   - Service name: `aurkbd`
   - Command `0`: returns shared-memory handle
   - Shared region contains bridge state/event ring
5. Write debug log to:
   - `sdmc:/config/breezehand/aurora_kbd_bridge.log`

## Current Limitations

- This is an MVP bridge; it currently decodes boot keyboard reports only.
- Vendor/NKRO custom report formats still need a parser layer.
- Transport is shared memory (IPC handle via `aurkbd` service).
- Input forwarding is currently used by Breezehand keyboard dialogs (text/numpad editors).

## Build

From this directory:

```bash
make.exe -j4
```

Output: `aurora_kbd_bridge.nsp`.

## Install (Atmosphere)

1. Copy `aurora_kbd_bridge.nsp` to:
   `sdmc:/atmosphere/contents/010000000000BD01/exefs.nsp`
2. Create:
   `sdmc:/atmosphere/contents/010000000000BD01/flags/boot2.flag`
3. Reboot.

## Next Steps

1. Add parser for 64-byte vendor reports from `MI_00`/`MI_03`.
2. Add IPC service (`aurkbd`) so Breezehand can read state without filesystem polling.
3. Extend usage mapping beyond current text/editor controls if needed.
