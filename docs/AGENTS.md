# Two agents, one tree

Doc runs more than one Claude session on this repository at once. They
share a working directory, so without a convention they overwrite each
other's staging, sweep each other's half-finished work into commits, and
ship changes the other never hears about. All three have happened.

This is the convention.

## Who owns what

| Area | Owner |
|---|---|
| `doc/guide/` — the handbook, its figures and build scripts | **handbook** |
| `docs/*.md` except the notes below | **handbook** |
| `core/`, `rom/`, `sdl/`, `pi/`, `demo/`, `forth/`, `test/`, `Makefile` | **coding** |
| `fs/` (the machine's own files) | **coding** |
| `README.md`, `CREDITS.md`, `LICENSES.md`, `THIRD_PARTY_SOURCES.md` | either — say so in your note first |
| `docs/BUILD-LOG.md` | either — append only, never rewrite |

Working outside your area is fine when it is small and you say so in
your note. Rewriting someone else's file without telling them is not.

## The notes

Each agent keeps one file and **only ever writes to its own**:

- `docs/notes/coding.md`
- `docs/notes/handbook.md`

Different files never conflict in git, which is the whole point. Read
the other's before you start and after you pull.

Each note carries: what I am doing now, what I have finished that the
other may need to act on, and what I am waiting on. Keep it short and
current — it is a status board, not a diary. `docs/BUILD-LOG.md` is the
diary.

## Three rules that came from real damage

1. **Stage explicit paths. Never `git add -A`.** A coding-session commit
   once swept up a rename that was in flight in the handbook session,
   and pushed a tree that did not build from a clean clone to four
   machines. `git status` before every commit; if you see files you did
   not touch, stop.

2. **Announce anything user-visible.** The BASIC sprite statements
   shipped and the handbook did not hear about them for days. If you
   add a command, a key, a menu entry or a register, put it in your note
   under "for the other agent" the same day.

3. **Do not rebuild what you do not own.** Building writes object files
   and binaries into the shared tree. If you need the other's area
   built, say so in your note rather than running their build.
