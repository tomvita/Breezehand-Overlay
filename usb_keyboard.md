# USB Keyboard Support in Breezehand

This document describes how USB keyboard input is supported in Breezehand.

## Scope

USB keyboard support currently covers:

- Global menu/navigation input (through Tesla input mapping)
- Direct text input in `KeyboardGui`

It does not attempt to emulate every possible keyboard layout or IME behavior.

## Implementation Overview

USB keyboard support is implemented in two layers:

1. Global keyboard-to-controller mapping in Tesla:
   - File: `lib/libultrahand/libtesla/include/tesla.hpp`
   - Function: `mapKeyboardHeldToController`
   - Purpose: allow keyboard control in normal menus that expect controller keys.

2. Direct physical keyboard text handling in `KeyboardGui`:
   - Files: `source/main.cpp`, `source/keyboard.hpp`
   - Purpose: allow typing characters directly (including symbols not present on the virtual keyboard UI).

## Initialization

Keyboard services are initialized via:

- `hidInitializeKeyboard()`
- `hidEnableUsbFullKeyController(true)`

Keyboard states are read with:

- `hidGetKeyboardStates(...)`
- `hidKeyboardStateGetKey(...)`

## Global Menu Mapping (Tesla Layer)

Current global mappings:

- Arrow keys -> `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`
- Main Enter (plain, no Shift) -> `KEY_A`
- Escape -> `KEY_B`
- Plus/Minus -> `KEY_PLUS` / `KEY_MINUS`

Important:

- `NumPadEnter` is not globally mapped to `KEY_A`.
- `Shift+Enter` is not globally mapped to `KEY_A`.
- This is intentional so `KeyboardGui` can reserve those for confirm/end-edit.

## KeyboardGui Behavior

`KeyboardGui` has direct physical keyboard input support.

### Confirm / Select behavior

- Main Enter (plain): behaves like `KEY_A` selection/navigation in the keyboard UI.
- NumPadEnter: confirm/end edit (`handleConfirm()`).
- Shift+Enter: confirm/end edit (`handleConfirm()`).

### Other control keys

- Escape: cancel/back
- Backspace: delete previous character
- Left/Right arrows: move cursor
- Insert: toggle insert/overtype
- CapsLock: toggles virtual caps visual mode (text keyboard mode)

### Character input model

Physical input is treated as direct input (not virtual button simulation).

- Case from physical keyboard is preserved.
- Shift-modified symbols are supported.
- For `SEARCH_TYPE_TEXT`, printable ASCII (and tab) is accepted directly.
- For numeric/hex modes, characters are constrained to allowed characters for that mode.

## Supported Character Examples

Examples of mapped direct characters include:

- Letters: `a-z`, `A-Z`
- Digits: `0-9`
- Shifted digits: `!@#$%^&*()`
- Symbols: `_ + { } | : " ~ < > ? / \\ ; ' , . - = [ ]`
- Numpad operators: `/ * - + . ,`

## Limitations

- Behavior depends on whether the keyboard dongle is recognized by Switch HID.
- Non-standard or vendor-specific receivers may work on PC but fail on Switch.
- International layouts and non-ASCII text input are not fully handled.
- Some keys with platform/layout-specific meanings may not be mapped.

## Files Touched for USB Keyboard Support

- `source/keyboard.hpp`
- `source/main.cpp`
- `lib/libultrahand/libtesla/include/tesla.hpp`

## Troubleshooting

If keyboard input does not work:

1. Verify the updated `.ovl` was actually deployed to Switch.
2. Confirm the receiver is recognized as a USB HID keyboard on Switch.
3. Test both:
   - Global menu navigation (arrows, Enter, Escape)
   - `KeyboardGui` direct text input
4. If global navigation works but text entry does not, focus on `KeyboardGui` handling.
5. If nothing works, focus on HID enumeration/receiver compatibility.
