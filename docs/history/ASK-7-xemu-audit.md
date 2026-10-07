# ASK-7 — Xemu CPU Core Entanglement Audit

*Read on 2026-08-21 against a fresh shallow clone of
`github.com/lgblgblgb/xemu` (master). Line numbers refer to that
snapshot.*

## Verdict

**Clean. The 45GS02 scope call stands, and the port is smaller than
the plan budgeted for.**

The CPU core reaches memory and the hypervisor exclusively through
callbacks. There is no hypervisor entry/exit woven into instruction
dispatch; the word `in_hypervisor` appears **twice** in 2,927 lines,
both times as an interrupt-inhibit guard. Reset is a plain 6502-style
`PC = readWord($FFFC)`. The "trimming" the plan feared is roughly a
dozen lines of deletions plus one `#ifdef`.

---

## 1. What the core is

A single file, `xemu/cpu65.c` (2,927 lines) + `cpu65.h` (168 lines).
It implements **NMOS 6502, 65C02, 65CE02/4510 and the MEGA65 45GS02
from one source**, selected by compile-time macros:

| Macro | Effect |
|---|---|
| `CPU_65CE02` | Z register, 16-bit SP, base-page register, 65CE02 opcodes |
| `MEGA65` | Q pseudo-register, `NEG NEG` / `NOP` prefixes, 32-bit flat addressing, 40.5 MHz timing table, NMOS persona |
| `CPU_6502_NMOS_ONLY` | Plain 6502 (VIC-20 etc.) |
| `CPU65_TRAP_OPCODE` | Optional: one opcode escapes to `cpu65_trap_callback()` |

Xemu's own C65 target builds it with `CPU_65CE02` alone; the MEGA65
target adds `MEGA65`. **That means the 4510-without-extensions build
already exists and is exercised** — we are not the first to compile
this core with the MEGA65 half off. We are merely the first to want it
*on* without the MEGA65 around it.

The author's own header comment is worth quoting for calibration:
*"THIS IS AN UGLY PIECE OF SOURCE REALLY … kinda buggy
overall-behavioural emulation, ie there is some on-going tries for
correct number of cycles execution, but not for in-opcode timing."*
Under §0.5 that is a description of exactly what we want — architectural
correctness, cycle counts best-effort — not a warning.

## 2. The memory interface — fully callback-based

Every memory access in the core goes through five macros
(`cpu65.c:182–190`):

```c
#define readByte(a)                    cpu65_read_callback(a)
#define writeByte(a,d)                 cpu65_write_callback(a,d)
#define writeByteTwice(a,od,nd)        cpu65_write_rmw_callback(a,od,nd)
#define readFlatAddressedByte()        cpu65_read_linear_opcode_callback()
#define writeFlatAddressedByte(d)      cpu65_write_linear_opcode_callback(d)
```

The core **never touches a memory array**. It does not know how big
memory is, where ROM is, what `MAP` does, or that a hypervisor exists.
The 16-bit `addr` in the first three is the CPU-visible address; the
*target* resolves it through its own MMU. The two `linear` callbacks
take no address at all — the target reads the 32-bit pointer out of the
zero page itself (`memory_mapper.c:1046–1064`) and applies the
`& 0xFFFFFFF` 28-bit mask. **The 28-bit address space lives entirely in
the target, not the core.**

This is the best possible answer to the question ASK-7 asked.

## 3. Every MEGA65-specific site in the core, classified

`grep -n MEGA65 cpu65.c` gives ~120 hits. They fall into four groups:

**(a) Q-opcode and flat-addressing implementations — ~100 hits, KEEP.**
Lines 695–2810: `ORQ`, `ASLQ`, `LDQ`, `STQ`, `ADCQ`, `CMPQ`, `INQ`,
`DEQ`, `NEGQ`, `ROLQ`, `RORQ`, `ASRQ`, `LSRQ`, `BITQ`, `ANDQ`, `EORQ`,
`SBCQ`, `CPMQ`, plus every `[$nn],Z` base-page-indirect-32 form. Each
is a small `if (IS_NEG_NEG_OP()) {…}` branch inside the normal opcode
case. These are the reason for choosing the donor; they come across
untouched.

**(b) Prefix state machine and Q register helpers — KEEP.**
`PREFIX_*` constants and `AXYZ_GET/SET` (lines 84–104); the
`CPU65.prefix = PREFIX_NOTHING` reset after each opcode (2828–2831);
the `EOM`-as-prefix handling at 2622–2625. All self-contained.

**(c) Timing — KEEP or IGNORE, our choice.**
`cpu65_mega65_timings.h` (85 lines) supplies the 40.5 MHz cycle table;
`cpu65_set_timing()` (lines 122–160) selects between C65-slow,
65CE02-fast and MEGA65-ultra tables. Under §0.5 we can keep the ultra
table and never call the switch. Zero cost either way.

**(d) Hypervisor coupling — 3 sites, DELETE.**

| Line | Code | Action |
|---|---|---|
| 53–55 | `#ifdef MEGA65 #include "hypervisor.h"` | Delete the include |
| 881–882 | NMI gate: `&& !in_hypervisor && CPU65.prefix == PREFIX_NOTHING` | Delete the `in_hypervisor` term; **keep** the prefix term |
| 906–907 | IRQ gate: same | Same |

That is the entire hypervisor footprint in the core. The `prefix`
half of each guard is real 45GS02 behaviour (no interrupts mid-prefix)
and stays.

**(e) Mode-selection macros — 1 site, TRIM.**
Lines 227–235 define `IS_CPU_NMOS` as a runtime test of
`CPU65.nmos_mode` under `MEGA65` (the MEGA65 can switch to an NMOS
6502 persona for C64 mode). We have no C64 mode. Define `IS_CPU_NMOS`
as constant `0` as the C65 build does and the compiler removes every
NMOS branch. One `#ifdef` change.

## 4. Reset, MAP and traps — where the hypervisor *would* have been

- **Reset** (`cpu65_reset`, line ~380): clears registers, sets
  `sphi=$0100`, `bphi=0`, prefix=nothing, then `PC = readWord($FFFC)`.
  **6502-style. No hypervisor.** The MEGA65's hypervisor-owns-reset
  behaviour is implemented in the *target* by mapping Hyppo ROM over
  the vector at reset time — the core never knows. The design doc's
  worry that "reset must come back to a 6502-style vector … one of the
  few places the trimming is not merely deletion" is withdrawn: there is
  nothing to come back from.
- **MAP ($5C)**, line 1687–1691: calls `cpu65_do_aug_callback()` and
  nothing else. The MMU is the target's. Xemu's C65 target implements
  the 20-bit version in 8 lines (`commodore_65.c:418–426`); the MEGA65
  target's 28-bit version (`memory_mapper.c:790–830`) adds the
  `X==$0F` / `Z==$0F` mega-byte-select convention. **We copy the
  MEGA65 version and drop its one `in_hypervisor` debug warning.**
- **Hypervisor traps** (`$D640–$D67F` writes on the MEGA65) are not
  opcodes; they are I/O writes handled in the target's memory decoder.
  The core has an *optional* `CPU65_TRAP_OPCODE` hook (line 941–946)
  which the MEGA65 target does not use. Irrelevant to us either way.

## 5. What the target has to supply

This is the actual Phase 2/3 work, and it is ours to write regardless
of donor — the audit only tells us its shape. Eleven functions:

```
cpu65_read_callback(addr16)                 16-bit → MMU → 28-bit → RAM/IO
cpu65_write_callback(addr16, data)
cpu65_write_rmw_callback(addr16, old, new)  (or define CPU65_NO_RMW_EMULATION)
cpu65_read_linear_opcode_callback()         ZP[Z] 32-bit ptr → & 0xFFFFFFF → RAM
cpu65_write_linear_opcode_callback(data)
cpu65_do_aug_callback()                     MAP: the 28-bit MMU state change
cpu65_do_nop_callback()                     EOM: re-enable interrupts after MAP
cpu65_illegal_opcode_callback()
cpu65_trap_callback(op)                     only if CPU65_TRAP_OPCODE is set
cpu65_nmi/irq/execution_debug_callback()    monitor hooks; can be stubs
```

Everything else — the 256 MB array, lazy page commit, the I/O decode,
VICKY's register window — sits behind those. The MEGA65's
`memory_mapper.c` (1,243 lines, 32 hypervisor references) is the file
we do **not** take; it is the MEGA65 platform. We write our own, far
smaller, because our memory map has no C64/C65 compatibility banking
to honour.

## 6. Fit with VICE 3.3

The plan says "reshaped to VICE's `*core.c` macro-dispatch convention
so it slots in like `65816core.c`". Worth reconsidering after reading
the code: VICE's cores are *included* into a machine-specific `cpu.c`
that defines `LOAD()`/`STORE()` macros around them. Xemu's core is
*linked* against callbacks. These are the same idea with the polarity
flipped, and **the callback form is the simpler one to host** — the
VICE-side `k4510cpu.c` just implements the eleven functions above
against VICE's memory arrays and calls `cpu65_step()` from the
mainloop. No reshaping of the 2,927 lines is needed; the wrapper is the
work. Licence: GPL2+ both sides.

Two Xemu-isms to provide in a shim header (`emutools_basicdefs.h` is
the only include): `Uint8/16/32`, `XEMU_INLINE`, `XEMU_LIKELY/UNLIKELY`,
`XEMU_UNREACHABLE`, `DEBUG()`/`DEBUGPRINT()`. Trivial.

## 7. Known gaps, from the author's own TODO and from reading

- Illegal NMOS opcodes unimplemented — irrelevant, we run native only.
- "Incorrect timings in many cases" — irrelevant under §0.5.
- Snapshot support explicitly incomplete for prefix state
  (line 2846–2851) — matters for Phase 6 save-states; budget it.
- `cpu65_debug_set_pc()` spins up to 100 steps to get out of a prefix
  or MAP-inhibit window before moving PC — the monitor will need the
  same care. Already written, just be aware of it.

## 8. Revised Phase 2 estimate

| Item | Plan said | Audit says |
|---|---|---|
| Trim hypervisor from core | "the real trimming effort" | 3 sites, ~6 lines |
| Trim MEGA65 I/O from core | part of trimming | **0** — not in the core at all |
| Reset-vector handling | "not merely deletion" | nothing to do |
| Reshape to VICE macro-dispatch | assumed | unnecessary; callback wrapper instead |
| MMU (28-bit MAP) | Phase 3 | copy ~40 lines from `memory_mapper.c:790`, write the rest |
| The memory system itself | Phase 3 | unchanged — this is the real work, and it was always ours |

**Phase 2a (this audit) is complete. Phase 2b is a wrapper, not a
port.**

---

## Decision

The 45GS02 scope call in §0 of the design document is **confirmed**,
with one sentence to correct there: the reset-vector caveat is
withdrawn. Risk table row "Xemu core entanglement — Medium–High" drops
to **Low**.
