# Filing an Issue

This machine has one author, and an issue you file will, in all likelihood, be handed straight to an AI coding session to work on, as written. That is worth knowing, because it changes what a good report is: not a description of the fault, but the machine’s own account of it. The machine can write most of that account itself, and the whole of this appendix is how to let it.

## Before you try again

None of these is politeness. Each one removes a day.

**Turn `DUMP ON` before you reproduce it.** Then every fifteen seconds, and again the instant you type `DUMP` once it has gone wrong, the machine writes its whole state — registers, the screen as you see it, the last 4096 instructions, your keystrokes, and the log of every command you have typed — to `dumps/dump-NNN.txt` on the host. A dump taken while it is wrong is worth more than any description either of us could write, and `DUMP ON` means you cannot miss the moment.

**Try it once with `k4510 ``-``-``no-startup.bat`.** A `/STARTUP.BAT` that has gone wrong looks exactly like a machine that has gone wrong, and telling them apart has cost this project a day before. Ten seconds of yours settles it.

**Check this book.** If the machine disagrees with a page here, that is still a fault worth filing — it is this book’s, and it is fixed the same way. Say which page.

## BUG writes the report

Then let the machine do the writing:

    /] BUG

`BUG` interviews you — seven questions, one at a time, each answered in a line or two — and writes the finished report to

<div class="center">

`/SYSTEM/LOG/BUGREPORTS/BUG-YYYYMMDD-HHMMSS.TXT`

</div>

making the directory if it is not there. Five lines it fills in itself, and they are the five most often got wrong by hand: which machine (the K4510 on its own Linux, or in a window on somebody’s desktop), the exact build (`INFO -v` says the same — a trailing `+` means the emulator was built from an edited tree, which is exactly the report that will not reproduce), the screen you are in, the last dump, and when.

The questions are the ones a coding session would otherwise have to ask you: where you were (the shell, a BASIC, CP/M…), what you typed *exactly*, what happened, what you expected instead, and *why* you expected it — the line most reports leave out. The K4510 is a fantasy machine ([Disclaimer](z1-disclaimer.md)): there is no silicon it can be measured against, so “wrong” only means something next to what you were expecting and where that came from. It is also what separates a bug from a design decision from an error in this book, which are three different repairs.

`BUG` is a program rather than a command, so it reaches you everywhere the `*` escape does: `*BUG` from either BASIC, from the monitor, from CP/M.

!!! note ""
    **`DUMP` first, then `BUG`.** If the report’s `Dump` line says there is none, the more valuable half is missing. Type `DUMP` while it is still wrong and run `BUG` again — the two travel together, and this is the one thing that is easy to do in the wrong order.

## Where it goes

<div class="center">

<https://github.com/mlongval/k4510/issues>

</div>

From the host, the report is in `fs/SYSTEM/LOG/BUGREPORTS/` and the dump in `dumps/`: open an issue, attach both, and you are done — the report is the whole text of the issue. The form there asks `BUG`’s questions again for anyone who has no report file: a fault in this book, a machine that will not boot, a telephone. If you have no account there, the same two files in an email are worth exactly as much.

Feature requests and disagreements are welcome in the same shape — “what I expected” does the work whether or not anything is broken.

!!! note ""
    **One thing to check before you attach a dump.** It carries the shell log, which is every command line you typed in that session, and file names from your own disk along with them. It is a plain text file: open it and have a look before it goes anywhere public.
