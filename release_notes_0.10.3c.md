# Breezehand 0.10.3c - User Release Notes

## Highlights

- `breezehand_watch.ovl` works again with the current dmnt.gen2 fork (v0.20).
- It recognises Breeze's field, method and code watches instead of misreading them.

## What You Will Notice

### Watch no longer reports a gen2 version mismatch

The watch overlay checked for dmnt.gen2 v0.15 exactly, so any newer fork
installed by Breeze (v0.16 through v0.20) was treated as unknown and the Watch
feature stayed off.

It now accepts v0.15 and anything newer. The newer forks only added fields
after the part of the watch data the overlay uses, and the fork fills those
in with zeros when the overlay sends its shorter block, so watches set from
the overlay behave exactly as before.

### Breeze's field, method and code watches are reported, not misread

Breeze can now run watches the overlay has no view for: field watch, field
sweep, method watch and code watch. The overlay used to read their results as
if they were ordinary watch rows, which showed nothing or a list of nonsense
addresses. It now names the watch Breeze is running and gives its one-line
total (bytes accessed, methods called, or functions called), and points you
to Breeze for the results.

L3+Y (Trace) does nothing while one of these watches runs. Trace breaks
through an ordinary watch, so on these it would have stopped Breeze's watch
and then waited for a break that never comes.

## Files

Same six overlays as 0.10.3b: `breezehand.ovl`, `breezehand_light.ovl`,
`bookmark.ovl`, `breezehand_watch.ovl`, `editcheat.ovl`, `editcheatk.ovl`.
