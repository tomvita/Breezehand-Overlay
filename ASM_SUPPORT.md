# ASM Keyboard Assembly Support

This document describes what the `Edit ASM` keyboard currently accepts when assembling text back into a 32-bit opcode.

Parsing is case-insensitive and whitespace-tolerant.

## Optional Keystone Backend

- The project now supports an optional Keystone assembler backend.
- Build with:
  - `make USE_KEYSTONE_ASM=1`
- When enabled:
  - The ASM editor first tries Keystone for broad instruction support.
  - If Keystone fails, it falls back to the built-in hand-coded subset documented below.
- When disabled (default):
  - Only the built-in subset is available.

Memory behavior:
- Keystone engine is lazily initialized on first assembly attempt.
- Single-instruction assembly only.
- Encoded buffers are freed immediately after use.

## General Input

- Raw hex opcode is accepted directly:
  - `D503201F`
  - `0xD503201F`
- If the input text exactly matches the current disassembled text (ignoring case/spacing normalization), the current opcode is kept.

## Supported Instructions

## System / Hints

- `nop`
- `yield`
- `wfe`
- `wfi`
- `sev`
- `sevl`

## Branch Register

- `ret`
- `ret xN`
- `br xN`
- `blr xN`

Notes:
- `xN` currently allows `x0..x30`.

## Trap / Exception

- `svc #imm16`
- `hvc #imm16`
- `smc #imm16`
- `brk #imm16`
- `hlt #imm16`

## Unconditional Branch (immediate)

- `b #offset`
- `bl #offset`
- `b 0xADDRESS`
- `bl 0xADDRESS`

Notes:
- Offset must be 4-byte aligned.
- Range is limited by instruction encoding (26-bit signed immediate << 2).

## Conditional Branch (immediate)

- `b.eq #offset` / `b.eq 0xADDRESS`
- `b.ne #offset` / `b.ne 0xADDRESS`
- `b.lt #offset` / `b.lt 0xADDRESS`
- `b.gt #offset` / `b.gt 0xADDRESS`
- `b.hi #offset` / `b.hi 0xADDRESS`
- `b.lo #offset` / `b.lo 0xADDRESS`

Notes:
- Offset must be 4-byte aligned.
- Range is limited by instruction encoding (19-bit signed immediate << 2).

## Integer ALU

### Register forms

- `add xD, xN, xM`
- `add wD, wN, wM`
- `adds xD, xN, xM`
- `adds wD, wN, wM`
- `sub xD, xN, xM`
- `sub wD, wN, wM`
- `subs xD, xN, xM`
- `subs wD, wN, wM`
- `cmp xN, xM`
- `cmp wN, wM`

Optional shift on register operand:
- `..., lsl #N`
- `..., lsr #N`
- `..., asr #N`

### Immediate forms

- `add xD, xN, #imm`
- `add wD, wN, #imm`
- `adds xD, xN, #imm`
- `adds wD, wN, #imm`
- `sub xD, xN, #imm`
- `sub wD, wN, #imm`
- `subs xD, xN, #imm`
- `subs wD, wN, #imm`
- `cmp xN, #imm`
- `cmp wN, #imm`

Optional immediate shift:
- `..., lsl #12`

Notes:
- Immediate is encoded as 12-bit with optional left shift by 12.

## MOV

### Register forms

- `mov xD, xM`
- `mov wD, wM`

### Immediate forms

- `mov xD, #imm16`
- `mov wD, #imm16`
- `mov xD, #imm16, lsl #16|#32|#48`
- `mov wD, #imm16, lsl #16`

Notes:
- This path currently uses MOVZ-style immediate encoding only.

## Load / Store (unsigned immediate)

- `ldr xT, [xN]`
- `ldr xT, [xN, #imm]`
- `ldr wT, [xN]`
- `ldr wT, [xN, #imm]`
- `str xT, [xN]`
- `str xT, [xN, #imm]`
- `str wT, [xN]`
- `str wT, [xN, #imm]`
- `ld xT, [xN, #imm]` (alias of `ldr` in parser)
- `st xT, [xN, #imm]` (alias of `str` in parser)
- `ldrb wT, [xN]`
- `ldrb wT, [xN, #imm]`
- `ldrh wT, [xN]`
- `ldrh wT, [xN, #imm]`

Notes:
- Base register also supports `sp`.
- Immediate must satisfy alignment/scaling:
  - `ldr/str w`: multiple of 4
  - `ldr/str x`: multiple of 8
  - `ldrb`: multiple of 1
  - `ldrh`: multiple of 2

## LDR Literal

- `ldr wT, #offset`
- `ldr xT, #offset`
- `ldr wT, #0xADDRESS`
- `ldr xT, #0xADDRESS`

Notes:
- `#0xADDRESS` is treated as absolute and converted to PC-relative offset.
- Offset must be 4-byte aligned and in 19-bit signed immediate range.

## Floating Point

## FMOV

### Register forms

- `fmov sD, sN`
- `fmov dD, dN`
- `fmov wD, sN`
- `fmov sD, wN`
- `fmov xD, dN`
- `fmov dD, xN`

### Immediate forms

- `fmov sD, imm`
- `fmov sD, #imm`
- `fmov dD, imm`
- `fmov dD, #imm`

Notes:
- Only ARM64-representable FP-immediate values are accepted.

## FP 3-register arithmetic

- `fadd sD, sN, sM`
- `fadd dD, dN, dM`
- `fsub sD, sN, sM`
- `fsub dD, dN, dM`
- `fmul sD, sN, sM`
- `fmul dD, dN, dM`
- `fdiv sD, sN, sM`
- `fdiv dD, dN, dM`

## Known Limits / Not Yet Supported

- No labels/symbol resolution.
- No multi-line assembly in one submit.
- No generic assembler backend; support is a hand-coded subset.
- Many valid ARM64 instructions are still unsupported.
