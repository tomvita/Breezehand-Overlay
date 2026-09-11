# Breezehand 0.10.3b - User Release Notes

## Highlights

- `bookmark.ovl` and `breezehand_watch.ovl` now read the current Breeze bookmark
  file format (`BREEZE00F`, Breeze beta108.8+3b and later).
- Bookmark files written by older Breeze builds (`BREEZE00E`) still load.
- `breezehand_light.ovl` is back in the release zip.

## What You Will Notice

### Bookmarks from current Breeze show up correctly again

Breeze beta108.8+3b raised the maximum pointer depth from 12 to 15, which made
every bookmark record 24 bytes longer on disk (184 bytes instead of 160) and
changed the file magic from `BREEZE00E` to `BREEZE00F`. Breezehand was still
reading the old layout, so files written by any recent Breeze were rejected or
read at the wrong record boundaries.

The reader now picks the record layout from the file magic. A file from current
Breeze loads with the 184-byte layout; a file from an older Breeze loads with
the 160-byte layout and each entry is converted on the way in, so the rest of
the overlay sees one shape regardless of which Breeze wrote the file. Labels,
value types, pointer chains and the memory-region base all resolve as before.

This was checked against real files pulled from a Switch: one current-format
file with 12 entries and two old-format files with 12 and 2187 entries all parse
with no leftover bytes.

### Old bookmark files are still readable here, not in Breeze

Breeze itself no longer opens an old-format `.bmk`; it renames the file to
`.bmk.legacy` and starts a fresh one. Breezehand does not do that, so an
old-format file next to the current one remains viewable in the bookmark
overlay until you replace it.

### Light overlay included in the zip

With a parallel build the `breezehand_light.ovl` copy could race against the
main overlay's clean step and be dropped from `out/`, so the release zip shipped
without it. The build now orders the two steps, and the zip contains all six
overlays: `breezehand`, `breezehand_light`, `bookmark`, `breezehand_watch`,
`editcheat`, `editcheatk`.

## Compatibility Notes

- Bookmark overlays are read-only; nothing in this release changes how Breeze
  writes or migrates bookmark files.
- Breezehand releases before 0.10.3b cannot read `BREEZE00F` files.

## Upgrade Note

- Deploy the `.ovl` files from this release (`0.10.3b`) to
  `sdmc:/switch/.overlays/` to get the bookmark fix.
