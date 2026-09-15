# A K4510 code page — the plan

Status: **steps 1-2 done**, 2026-09-15. Doc chose the German quotes over
Portuguese. The table is `core/codepage.h` (from `tools/mkcodepage.py`), the
fonts are built from it by `tools/mkk4510font.py`, and JIM, the keyboard, the
keymaps (dead key + capital, 38 compositions -> 55) and `k4510-remote` read it.
The CP437 fonts are kept as `font8-cp437.bin` / `font16-cp437.bin`. Still to
do: steps 3-5 (the strict-CP437 bank for a BBS, `k4510-screen --utf8`, the
audit of $E0-$FF use, the handbook appendix).

## Why

The machine's character set is IBM's code page 437 (the font, unscii, is laid
out in its order; JIM translates UTF-8 to it; the keyboard maps to it). CP437
was made for American English and box drawing. Checked against the keyboard
layouts the machine offers (`core/kbdmaps.h`), it cannot show:

| Language | Letters and marks with no place in CP437 |
|---|---|
| French (France, Canada) | œ Œ À Â È Ê Ë Î Ï Ô Ù Û Ÿ € |
| Italian | À È Ì Í Ò Ó Ù Ú € |
| Spanish | Á Í Ó Ú € |
| German | ẞ „ “ ‚ ‘ € (and § — see below) |
| US-intl (for all of these) | À Â È Ê Ë Î Ï Ô Ù Û Á Í Ó Ú € |

The lower-case letters of all of them are there, and so are É Ç Ñ Ä Ö Ü Å Æ.
So the gap is the accented capitals, œ/Œ, €, and the German quotes. Today a
typed character with no place is dropped without a sign
(`sdl/main.c cp437_of` returns 0).

**§ ¶ • ← ↑ → ↓** have glyphs in the font, but only at $01-$1F, which are
also the control codes: § is Ctrl-U, • is Ctrl-G, ↑ ↓ → are Ctrl-X Ctrl-Y
Ctrl-Z, and ← is Escape. A program cannot tell them from the keys, and JIM
obeys them rather than drawing them. They need upper-half places too.

## The idea

Keep CP437 wherever it is used — the box drawing, the shades and blocks, and
the accented letters already there — and put the missing letters in the
places of CP437's least-used characters: the Greek and mathematical symbols
at $E0-$FF and a few obsolete currency and odd marks. Unscii has every glyph
needed, so it is a new layout of the font, not new art.

### Kept exactly (so CP437 text, menus and box art still look right)

- $80-$A5: every accented letter (Ç ü é â ä à å ç ê ë è ï î ì Ä Å É æ Æ ô ö ò
  û ù ÿ Ö Ü á í ó ú ñ Ñ), £ ($9C), ¢ ($9B), ¥ ($9D)
- $A6-$AF: ª º ¿ ¬ ½ ¼ ¡ « »
- $B0-$DF: all box drawing, shades, blocks
- ß ($E1), µ ($E6), ± ($F1), ÷ ($F6), ° ($F8), · ($FA), ² ($FD), ■ ($FE), no-break space ($FF)

### Freed: 26 places

| Place | CP437 now | Place | CP437 now |
|---|---|---|---|
| $9E | ₧ (peseta) | $EC | ∞ |
| $9F | ƒ | $ED | φ |
| $A9 | ⌐ | $EE | ε |
| $E0 | α | $EF | ∩ |
| $E2 | Γ | $F0 | ≡ |
| $E3 | π | $F2 | ≥ |
| $E4 | Σ | $F3 | ≤ |
| $E5 | σ | $F4 | ⌠ |
| $E7 | τ | $F5 | ⌡ |
| $E8 | Φ | $F7 | ≈ |
| $E9 | Θ | $F9 | ∙ |
| $EA | Ω | $FB | √ |
| $EB | δ | $FC | ⁿ |

### What goes in them — a first proposal (26)

1. **The accented capitals (17):** À Â È Ê Ë Î Ï Ô Ù Û Ÿ Á Í Ó Ú Ì Ò —
   French, Italian, Spanish complete in both cases.
2. **œ Œ € (3)** — French, and the euro everywhere.
3. **§ ¶ (2)** — German and legal text; out of the control codes.
4. **„ “ (2)** — German quotes; " " ‘ ’ keep going to ' and ", as JIM does now.
5. **ø Ø (2)** — Danish and Norwegian complete.

That is 26. **Choices for Doc:**

- Portuguese needs ã õ Ã Õ (4 more). They could replace ¢ ¥ ¬ and one of
  ½ ¼, or „ “ could give way (German quotes falling back to ").
- • and the arrows ← ↑ → ↓ would take 5 more; the arrows could also stay out
  of text altogether (the keyboard sends them as keys, which is what they are).
- ẞ (capital sharp s) is rare enough to leave out.

With the 26 above, French, Italian, Spanish, German, Danish, Norwegian,
Swedish, Finnish, Dutch, Catalan, Galician, Basque, Irish, Scottish Gaelic,
Afrikaans and Albanian are complete in both cases.

## What CP437 would lose, and where that matters

The replaced symbols (α π Σ σ ≡ ≥ ≤ √ ∞ ⌠⌡ and so on) would draw as the new
letters. Where that could show:

- **A BBS over TELNET**, whose ANSI art is CP437. Box drawing and shades are
  kept, so most of it is unchanged; a screen using ≡ or √ would change.
- **CP/M software** (WordStar, Turbo Pascal) is 7-bit and unaffected.
- **Programs on the machine** that print those bytes (to audit: grep the
  demos, BASIC examples and the handbook for bytes $E0-$FF).

**The remedy: keep a strict CP437 font beside it.** JIM already switches fonts
for PETSCII; a second font bank (the current unscii CP437 layout, 4 KB + 2 KB)
selected by TELNET when it meets a BBS, and by a program that asks, keeps the
old world exact while the shell, the editors and the BASICs speak the new page.

## What changes

1. **The font** — `data/fonts/unscii/font8-unscii.bin`, `font16-unscii.bin`:
   rebuilt from `unscii-8.hex` / `unscii-16.hex` by a small generator
   (`tools/mkk4510font.py`, the table below as data), keeping $01-$1F's CP437
   pictures. The strict CP437 layout kept as `font8-cp437.bin` / `font16-cp437.bin`.
2. **The table, one file** — `core/codepage.h`: the 256 Unicode values of
   the K4510 page, used by everything below, so nothing can drift.
3. **JIM** (`core/term.c`): `cp437_hi` becomes the new table (UTF-8 in, from
   `!`, TELNET, ssh); `term_cp437_utf8` the other way (what the machine types
   to a Unix host). Near-matches (curly quotes to straight) stay.
4. **The keyboard** (`sdl/main.c cp437_of`): from the same table; every
   character a layout can type that the page has reaches the machine. The
   dead-key compositions (`tools/mkkbdmaps.py`) gain the capitals
   (dead ` + A → À and so on), which today only exist for É Ñ Ä Ö Ü Ç.
5. **What a character with no place does** — a decision: nothing (today),
   `?`, or the plain letter (Ă → A). Recommended: the plain letter, so text
   stays readable.
6. **The host side**: `tools/k4510-screen --utf8` and `k4510-remote` decode
   with the table instead of Python's `cp437`; files the machine writes stay
   bytes, and a `k4510-iconv` helper converts them for Linux.
7. **Tests**: `termtest` (UTF-8 in → byte → glyph, both fonts), a round trip
   for every table entry, and a remote test typing each language's letters
   through each layout.
8. **The handbook**: an appendix with the page as a table (generated from
   `core/codepage.h`, like the keyword reference).

## Order of work

1. `core/codepage.h` and the generator; the two fonts built; `termtest`.
2. JIM and the keyboard switched to the table; the dead-key capitals.
3. The strict-CP437 bank and TELNET's switch.
4. The host tools, the audit of $E0-$FF use, the handbook appendix.
5. On the Dell: a remote test per layout.

Estimate: steps 1-2 an evening's work; 3-5 another.
