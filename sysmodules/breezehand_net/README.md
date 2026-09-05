# breezehand-net

A small TCP command server that exposes the running game's memory -- search,
read, write, sampling, controller input and freeze -- to a scripting client on
the network.

Program id `4200000000000B00`, default port `6800`, **off by default**.

## Why it exists

The Atmosphere GDB stub halts the game on every attach. That makes it unusable
for observing anything that changes over a second or two, because every sample
steals game time from the window you are trying to observe. Chasing a hover
meter that drained over ~3 seconds produced **6 usable samples across an entire
hover**, which is not enough to tell a real drain from a value that happens to
be oscillating.

dmnt's cheat-process path reads and writes a *running* game. Measured on device:

| | GDB stub | breezehand-net |
|---|---|---|
| samples across one 2 s window | 6 | **120** (60 Hz, 0 failed) |
| memory scan rate | ~1.3 MB/s | **~550 MB/s** |
| game during the operation | frozen | running |

A 2.76 GB primary search completes in 5 seconds while the game plays normally.

## Design

**The sampling loop runs on-device.** `watch` fills a local buffer at the
requested rate and only ships the result afterwards. `dmntchtReadCheatProcessMemory`
is an IPC round trip; putting the network inside the sample loop would
reintroduce the exact problem this exists to solve.

**It never attaches to the game.** It calls `dmnt:cht` IPC and dmnt performs the
read with the debug handle it already holds. There is no kernel
one-debugger-per-process contention with a GDB session, and gen2's own comments
show concurrent IPC clients are an anticipated case.

**The search engine is shared, not duplicated.** `breeze::RunStartSearch` /
`RunContinueSearch` in `common/` are already headless and already driven off a
worker thread with `SearchRunControl` atomics. `search_bridge.cpp` is a thin C
shim over them.

**Triggers are on-device.** Reading the pad here means "start sampling when the
player presses B" has one 2 ms poll of latency instead of a human plus a network
round trip. Measured arm-to-press latency: 274-324 ms, consistently.

## Protocol

Line-based text, one client at a time.

```
ping
meta                                      pid, titleid, main/heap/alias extents
peek  <hexaddr> <len>                     -> +OK <hex>
poke  <hexaddr> <hexbytes>
watch <hexaddr> <len> <hz> <ms> [<hexmask> <timeout_ms>]
                                          samples on-device; optional button trigger
                                          -> header, one hex line per sample, then "."
buttons                                   current pad mask
waitkey <hexmask> <timeout_ms>            blocks until a fresh press (rising edge)
freeze [<hexmask> <timeout_ms>]           pause the game, optionally on a button
resume
hold   <hexaddr> [width]                  dmnt holds the value (no network in the loop)
unhold <hexaddr>
holds                                     list held addresses
search start <type> <mode> <hexv1> [hexv2]
search cont  <type> <mode> <hexv1> [hexv2]
search status | abort
search results <offset> <count> [level]   -> "<address> <value>" lines
quit
```

`type` is `searchType_t` (4 = u32), `mode` is `searchMode_t` (0 = `SM_EQ`,
6 = `SM_RANGE_EQ`, 9 = `SM_MORE`, 10 = `SM_LESS`, 12 = `SM_SAME`). Search levels
auto-chain as `bhnet0.dat`, `bhnet1.dat`, ... in `sdmc:/switch/Breeze/`.

Button masks are Switch HID bits, so B = `2`.

## Install

Ships as a file in Breeze's own folder; nothing enters `contents/` until the
user opts in. On first enable:

1. Pick an id: `4200000000000B00`, falling back through `..0B0F` if a directory
   exists whose `toolbox.json` names a different module.
2. Create `/atmosphere/contents/<id>/` with `exefs.nsp` and `toolbox.json`.
3. **Do not create `flags/boot2.flag`** -- that is what keeps it from starting
   at boot. `boot2` only launches flagged programs
   (`boot2_api.board.nintendo_nx.cpp:245`), while the loader resolves program
   ids only from `contents/` (`fs_code.cpp:116`), so `contents/` is required but
   autostart is not.
4. Persist the chosen id, and set `enable=1` in `/switch/breeze/config.ini`.

One build runs under **any** program id: `ldr_meta.cpp:236` overwrites the NPDM
ids with whatever id it is launched as, so the conflict fallback needs no NPDM
patching and no per-id builds.

```ini
[Breezehand-Net]
enable=0
port=6800
```

The module exits immediately if `enable` is not set, so a stray install opens no
port.

## Build

```
make DEVKITPRO=/c/devkitPro
```

Links `common/` for `dmntcht.c` and the search engine. `BREEZE_SEARCH_NO_ULTRA`
swaps the engine's single libultrahand call (a `mkdir`) for a plain one, so the
same source builds for the overlay and here.

`.bss` is ~1.55 MB (1 MB heap + buffers). `BREEZE_SCAN_BUFFER_BYTES` and
`BREEZE_OUTPUT_BUFFER_BYTES` shrink the engine's 2 MB/512 KB buffers to
512 KB/256 KB; raise them if scan speed matters more than coexisting with other
sysmodules.

## Traps hit while building this

- Under libnx fsdev a bare `/switch/...` path resolves against the *default
  device*, which a sysmodule may not have. Use `sdmc:/switch/...`.
- `bsdInitialize()` sets up the BSD service but does **not** register the socket
  device with newlib, so POSIX `socket()` returns -1. Either call
  `socketInitialize()` or use `bsdSocket`/`bsdBind`/`bsdRecv` directly. We do
  the latter, to avoid the resolver's static memory.
- ftpsrv's socket sizing is for bulk transfer. Copying it cost ~4 MB of `.bss`
  and stopped the module launching alongside other sysmodules.
- Do **not** skip `hidSetSupportedNpadIdType` / `padConfigureInput`. They take an
  AppletResourceUserId and configure *our* session's view, not the game's;
  without them the applet resource has no supported npad ids and every read
  comes back zero.
- `search_types.hpp` uses C++ default member initialisers, so a C translation
  unit cannot include it. The bridge builds the `Search_condition`.

## Known limitations

- A second TCP client connects (via the listen backlog) and then hangs instead
  of receiving `-ERR busy`. Needs an accept/worker split.
- `hold` uses dmnt's frozen addresses, which are applied at the cheat VM's rate.
  Against a value the game recomputes every frame this sawtooths rather than
  pinning -- observed 18/53/13/63 on a value that drains at ~14/s. For those,
  patch the code rather than freezing the value.
- No gen2 register capture yet. For an address with several writers, a gen2
  watch is the right tool; see `WATCHPOINT_BUG.md` in dmnt.gen2.
