# Where the documents are

One page so nobody has to hunt again. Everything below lives in this
repository and is versioned with the code.

## The handbook

**`doc/guide/k4510-guide.pdf`** — the built book, tracked in the repo.
This is always the current one; it is committed with whatever change
made it change. (Note `doc/` — the handbook — versus `docs/` — this
folder. One letter apart, sorry.)

- `doc/guide/k4510-guide.tex` — the book; chapters in
  `doc/guide/chapters/`, visual language in `doc/guide/style/k4510.sty`
- `doc/guide/make-guide.sh` — captures every screenshot from the running
  machine (`make-shots.sh`), then runs XeLaTeX twice. A picture that
  cannot be produced fails the build.
- **Built on ubuntu-s1 only.** The laptop has no TeX installed and no
  captured shots; `doc/guide/shots/` is generated, not tracked. Rebuild
  there, commit the PDF, `git pull` everywhere else.

## This folder — design records and the diary

| File | What it is |
|---|---|
| `BUILD-LOG.md` | **The diary. Read this first.** Every session, in order, with the reasoning — not just what changed. |
| `K4510-Design.md` | The machine's design document: what it is and why. `make-pdf.sh` / `make-epub.sh` render it. |
| `VICKY-SPEC.md` | The video chip: registers, modes, layers, sprites, blitter, SHEILA. |
| `VIDEO-OPTIONS.md` | The video routes considered before VICKY was chosen. |
| `CPU-CLOCK-POLICY.md` | The CPU clock as a policy, not a constant: measure the host at boot rather than trust a compiled-in default; the three-host sweep of 2026-08-27 as evidence; why calibration must run with sound on. |
| `PORTABILITY.md` | What the desktop and Pi builds share, and where the host seam is. |
| `FEATURES.txt` | The historical feature ballot (A–K), all built. Decisions now happen in conversation and land in `BUILD-LOG.md`. |
| `bmc-k4510-45gs02-decision.md` | Why the 45GS02 core, and why unchanged. |
| `ASK-7-xemu-audit.md` | The audit of what was taken from Xemu. |
| `images/`, `cover.png` | Curated pictures for the design document. |
| `check-edits.sh` | Sanity check over edits to the long documents. |

## Root of the repository

| File | What it is |
|---|---|
| `README.md` | Install, build, run, layout. The front door. |
| `LICENSE` | GPL-2.0, full text. The project's licence. |
| `LICENSES.md` | Every component, its path, and its terms. The legal record. |
| `CREDITS.md` | The thanks. Everyone whose work is in the machine. |

The handbook carries all three of the last as end-matter chapters
(Disclaimer, Thank You, Licences); those chapters and these files are
kept in step by hand — change one, check the other.

## What is *not* here

The archive folder on ubuntu-s1 (`~/Projects/K4510/`) keeps only what
does not belong in a public repository: full-resolution phone photos,
release zips and probe logs, font staging, and its own git history. It
holds no living document. If you find a copy of a document from this
folder over there, it is stale by definition.
