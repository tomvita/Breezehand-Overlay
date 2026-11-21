# Breezehand 0.10.5 - User Release Notes

## Highlights

- Code-patch cheats made by Breeze beta129.02 put the game's code back when
  you turn them off in Breezehand, also in a game whose code is in a loaded
  module.

## What You Will Notice

### Turning a code-patch cheat off puts the game's code back

A code-patch cheat changes instructions in the game's code. Turning such a
cheat off only stops dmnt writing it: the changed instructions stay in the
game, so the cheat keeps working until the game restarts. That is why cheat
files used to come with a separate "restore" cheat.

Breeze beta129.02 makes these cheats with the game's own instructions kept at
the top of the cheat, in a block that never runs. Breezehand now reads that
block: turning the cheat off writes the game's instructions back at once, so
the cheat really stops. One cheat can change many places; all of them go back.

- An instruction that holds something else by then (another cheat changed it)
  is left alone.
- In games whose code is in a loaded module (cheats with `04010000` lines),
  Breezehand works out where the module is from the game's master code, as
  Breeze does.

## Compatibility Notes

- Cheats in any other form behave exactly as before. A "- restore" cheat in
  the file still works if you prefer it.
- Breeze beta129.02 or later writes cheats in this form.

## Upgrade Note

Replace all six overlays: `breezehand.ovl`, `breezehand_light.ovl`,
`bookmark.ovl`, `breezehand_watch.ovl`, `editcheat.ovl`, `editcheatk.ovl`.
