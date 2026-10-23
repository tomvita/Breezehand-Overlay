# Breezehand 0.10.4 - User Release Notes

## Highlights

- Cheat files made by Breeze beta127 (from a cheat source file) work in
  Breezehand: ASM cheats, their code caves and mods.
- Breezehand finds your cheats in the same folder as Breeze, also when Breeze
  names game folders by title instead of title ID.

## What You Will Notice

### ASM cheats from a cheat source file work

Breeze beta127 can build a game's ASM cheats from a cheat source file
(`<TitleID>.ini`). Such a cheat is split in two: the hook you see in the list,
and its code cave, which Breeze keeps in a `[code cave]` folder at the end of
the cheat file and writes into the game itself.

Breezehand now does the same:

- Loading the cheat file keeps the `[code cave]` folder out of the cheat list,
  so it no longer takes cheat slots or opcode room, and writes the caves into
  the game.
- Turning a hook on writes its cave first if the game does not have it (after
  a game restart, for example). Without the cave the hook would do nothing.
- Turning a hook off puts the game's own instruction back at once, so the
  cheat really stops.
- Saving keeps the `[code cave]` folder in the file, so Breeze still has it.

### Mods: only one value on at a time

A mod is a cheat value offered as a list, for example `Gil earned x1
(default)`, `x2`, `x4`, `x10`. As in Breeze:

- Turning one value on turns the other values of that mod off.
- Turning a value off puts the default value back.

Breezehand knows which cheats belong to a mod from the game's `<TitleID>.ini`.
Without that file it still groups values named like `Move speed = 2.0`.

### Breezehand finds your game folder like Breeze

Breeze can name each game's folder in `/switch/breeze/cheats/` by title ID or
by the game's title (Breeze's "use title id" setting). Breezehand only looked
in the title ID folder, so with title names it found neither your cheats nor
your notes and bookmarks.

Breezehand now reads that setting from Breeze and works out the folder name
the same way Breeze does. If Breeze is not installed, it uses the title ID
folder as before.

## Compatibility Notes

- Cheat files without a `[code cave]` folder behave exactly as before.
- Breeze beta127.00 or later writes the cheat files this release is for.

## Upgrade Note

Replace all six overlays: `breezehand.ovl`, `breezehand_light.ovl`,
`bookmark.ovl`, `breezehand_watch.ovl`, `editcheat.ovl`, `editcheatk.ovl`.
