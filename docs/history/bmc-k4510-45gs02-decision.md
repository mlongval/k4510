# BMC-K4510 — CPU Target Decision: 4510 vs 45GS02

*Context document from a Claude.ai conversation, 2026-08-21. Intended as input for Claude Code sessions on the BMC-K4510 project.*

## Background

BMC-K4510 is a from-scratch CPU emulation effort for BMC64, borrowing CPU core code from the MEGA65 emulator project (xemu). Target: a 4510-class CPU with 1MB RAM. Question raised: should the project hold at the 4510 instruction-set level, or go full 45GS02?

## CPU lineage (for reference)

- **6502 → 65C02 → 65CE02 (CSG, 1988)**: base lineage.
- **4502**: Commodore's enhanced 65CE02 *core* designed for the unreleased C65 (1990–91). Instruction set core only. 8KB-granularity MAP instruction for memory relocation within 1MB.
- **4510**: the actual silicon in C65 prototypes — 4502 core plus integrated I/O and memory-management logic.
- **45GS02**: MEGA65's FPGA CPU, a strict extension of the 4510. Adds:
  - 32-bit pseudo-register **Q** (A/X/Y/Z combined), accessed via `NEG NEG` opcode prefix
  - 32-bit flat zero-page-indirect addressing via `NOP` prefix (e.g. `NOP LDA ($nn),Z`)
  - 28-bit address space (256MB) via extended MAP + "mega-byte" register
  - Hypervisor privilege mode (Hyppo); reset vector handled by hypervisor, not 6502-style
  - New opcodes: two-byte branch offsets, LDZ, PHX/PHY/PHZ, TAB, BRA, etc.
  - ~40.5MHz clock; some instructions cost 1–2 cycles more than a real 4502

Key design property: the 45GS02 extensions are prefix-based and invisible to 4502/4510 code. Full backward compatibility by construction.

## Decision: go full 45GS02 (instruction set)

Rationale:

1. **Donor code alignment.** The borrowed MEGA65 emulator core *already is* a 45GS02. Holding at 4510 means stripping/gating extensions — extra work now, permanent divergence from upstream, harder merges of future fixes.
2. **No compatibility cost.** 45GS02 is a strict superset; plain 4510 code runs identically.
3. **32-bit flat pointers.** With 1MB RAM, MAP's 8KB granularity is tedious; 32-bit ZP-indirect dereferencing anywhere in physical memory is a major QoL win for new software. This alone justifies the choice.
4. **Toolchain.** Living cross-dev tools target 45GS02: ACME (`!cpu m65`), ca65 (`CPU_45GS02`), 6502.Net. 4510-only tooling is essentially archaeology.
5. **Community gravity.** No meaningful C65 software base exists; all software for this platform will be new code, and new code in this lineage targets 45GS02.

## Scope caveat (WHY.md material)

"45GS02" names both an ISA and a platform. BMC-K4510 wants the ISA, not the platform. Explicitly **out of scope**:

- Hypervisor mode / Hyppo trap interface
- Mega-byte MAP extension for >1MB (moot with 1MB physical RAM)
- MEGA65 I/O personality (VIC-IV I/O mode key sequences, etc.)

Honest spec: **45GS02 instruction set (Q ops, 32-bit ZP indirect, full 4502 base) + C65-style 1MB memory model + no hypervisor + no MEGA65 I/O.** A coherent machine, not a half-MEGA65.

## Open task before committing

Audit how entangled the borrowed xemu core is with MEGA65 memory-decoder and hypervisor plumbing:

- If the CPU core uses clean read/write callback interfaces → minimal trimming needed.
- If hypervisor entry/exit is woven into instruction dispatch → that is where the real trimming effort lands. Determine this before declaring ISA scope final.
