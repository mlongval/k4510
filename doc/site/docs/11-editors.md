# The Editors

Two of them, because they answer different questions. `EDIT` is the one to reach for when a file wants a line changed and you want to be out again in ten seconds. `VI` is the one for a file too big to think about, and it is modal, so it expects you to have met `vi` before.

Both draw through JIM, the VT100 in hardware ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), and both take their keys raw from the ROM — an arrow arrives as one byte rather than an escape sequence to unpick, with a bit beside it saying that it *is* an arrow and not the accented letter that shares its code. Neither needs the Tube.

## EDIT

`EDIT name` opens a file, or starts an empty one if there is no such file yet. The whole text sits in memory below the program, so it holds about 22 KB — ample for a `STARTUP.BAT`, a `.SUB`, a BASIC listing or a letter. There are no modes: what you type goes in, and everything else is a control key.

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Key</strong></th>
<th style="text-align: left;"><strong>Does</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;">arrows</td>
<td style="text-align: left;">move by a character or a line</td>
</tr>
<tr class="even">
<td style="text-align: left;">Home, End</td>
<td style="text-align: left;">start and end of the line</td>
</tr>
<tr class="odd">
<td style="text-align: left;">PgUp, PgDn</td>
<td style="text-align: left;">a screenful</td>
</tr>
<tr class="even">
<td style="text-align: left;">Enter</td>
<td style="text-align: left;">split the line at the cursor</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Backspace</td>
<td style="text-align: left;">rub out to the left; at the start of a line, join it to the one above</td>
</tr>
<tr class="even">
<td style="text-align: left;">Delete</td>
<td style="text-align: left;">rub out under the cursor</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl-O <em>or</em> Ctrl-S</td>
<td style="text-align: left;">save</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl-X</td>
<td style="text-align: left;">leave — twice on a changed file, to throw the changes away</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl-R</td>
<td style="text-align: left;">renumber a BASIC program: 10, 20, 30…, and every <code>GOTO</code> with it</td>
</tr>
</tbody>
</table>

</div>

Accented letters type as themselves. The bar along the bottom carries the name, a `*` while there are unsaved changes, where the cursor is, and the keys to save and leave — and Ctrl-R too, on a BASIC file — so there is nothing to remember. Long lines slide sideways rather than wrap.

## VI

`VI name` is modal in the old way. In *normal* mode the keys are commands; a handful of them put you into *insert* mode, where what you type goes into the file; and Escape brings you back. The `:` line at the bottom is the third place, for the ex commands. The cursor says which of the three you are in: a block in normal mode, a bar while inserting, an underline on the `:` line. Accented letters go in like any other, in insert mode and after `r`.

### Modes

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Key</strong></th>
<th style="text-align: left;"><strong>Enters insert…</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>i</code></td>
<td style="text-align: left;">before the cursor</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>a</code></td>
<td style="text-align: left;">after the cursor</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>I</code></td>
<td style="text-align: left;">at the start of the line</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>A</code></td>
<td style="text-align: left;">at the end of the line</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>o</code></td>
<td style="text-align: left;">on a new line below</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>O</code></td>
<td style="text-align: left;">on a new line above</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>s</code></td>
<td style="text-align: left;">replacing the character under the cursor</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>S</code></td>
<td style="text-align: left;">replacing the whole line</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>C</code></td>
<td style="text-align: left;">replacing the rest of the line</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>c</code><em>motion</em></td>
<td style="text-align: left;">replacing what the motion covers; <code>cc</code> the whole line</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Esc</td>
<td style="text-align: left;">…and leaves it, back to normal mode</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:</code></td>
<td style="text-align: left;">opens the command line (from normal mode)</td>
</tr>
</tbody>
</table>

</div>

### Moving

A count goes in front of nearly anything: `5j` is five lines down, `10G` is line ten.

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Key</strong></th>
<th style="text-align: left;"><strong>Moves</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>h</code> <code>j</code> <code>k</code> <code>l</code></td>
<td style="text-align: left;">left, down, up, right — the arrows do the same</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>w</code></td>
<td style="text-align: left;">to the next word</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>b</code></td>
<td style="text-align: left;">back a word</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>e</code></td>
<td style="text-align: left;">to the end of the word</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>0</code></td>
<td style="text-align: left;">to the start of the line</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>^</code></td>
<td style="text-align: left;">to the first non-blank on the line</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$</code></td>
<td style="text-align: left;">to the end of the line</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>gg</code></td>
<td style="text-align: left;">to the first line</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>G</code></td>
<td style="text-align: left;">to the last line, or to line <em>n</em> with a count</td>
</tr>
<tr class="even">
<td style="text-align: left;">PgUp, PgDn</td>
<td style="text-align: left;">a screenful</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>/</code><em>text</em></td>
<td style="text-align: left;">to the next <em>text</em> — a plain substring, not a pattern</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>?</code><em>text</em></td>
<td style="text-align: left;">to the previous one</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>n</code></td>
<td style="text-align: left;">the same search again; <code>N</code> the other way</td>
</tr>
</tbody>
</table>

</div>

### Changing

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Key</strong></th>
<th style="text-align: left;"><strong>Does</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>x</code></td>
<td style="text-align: left;">delete the character under the cursor</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>X</code></td>
<td style="text-align: left;">delete the one to its left</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>d</code><em>motion</em></td>
<td style="text-align: left;">delete what the motion covers: <code>dw</code> a word, <code>d$</code> to the end</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>dd</code></td>
<td style="text-align: left;">delete the line</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>D</code></td>
<td style="text-align: left;">delete to the end of the line</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>y</code><em>motion</em>, <code>yy</code></td>
<td style="text-align: left;">copy (yank) the same, without deleting</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>p</code></td>
<td style="text-align: left;">put what was deleted or yanked, after the cursor</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>P</code></td>
<td style="text-align: left;">put it before</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>r</code><em>c</em></td>
<td style="text-align: left;">replace one character with <em>c</em></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>~</code></td>
<td style="text-align: left;">flip the case of one character</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>J</code></td>
<td style="text-align: left;">join the next line onto this one</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>u</code></td>
<td style="text-align: left;">undo — unlimited</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl-R</td>
<td style="text-align: left;">redo — also unlimited</td>
</tr>
</tbody>
</table>

</div>

Operators take any motion — `d2w` deletes two words, `y$` yanks to the end of the line — or double themselves to work on whole lines. A charwise operator **clamps to the line**: `dw` at the end of a line stops there instead of eating into the next one. That is deliberate, and so is the plain-substring search: `/foo` finds `foo`, and `.` `*` `[` mean themselves.

### The command line

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Command</strong></th>
<th style="text-align: left;"><strong>Does</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>:w</code></td>
<td style="text-align: left;">save</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:w</code> <em>name</em></td>
<td style="text-align: left;">save under another name</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:q</code></td>
<td style="text-align: left;">leave — refuses on a changed file</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:q!</code></td>
<td style="text-align: left;">leave anyway, changes lost</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:wq</code>, <code>:x</code></td>
<td style="text-align: left;">save and leave</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:s/</code><em>old</em><code>/</code><em>new</em><code>/</code></td>
<td style="text-align: left;">replace the first <em>old</em> on this line (<code>/g</code>: every one)</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:%s/</code><em>old</em><code>/</code><em>new</em><code>/g</code></td>
<td style="text-align: left;">the same on every line of the file</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:map</code> <em>keys</em> <em>result</em></td>
<td style="text-align: left;">define a key sequence, in normal mode</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:imap</code> <em>keys</em> <em>result</em></td>
<td style="text-align: left;">the same, in insert mode</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:renum</code> [<em>start</em> [<em>step</em>]]</td>
<td style="text-align: left;">renumber a BASIC program (10 and 10 unless told), and every <code>GOTO</code> with it; <code>u</code> puts it back</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:make</code></td>
<td style="text-align: left;">save, compile a <code>.C</code> or <code>.PAS</code>, and go to the first error</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:run</code></td>
<td style="text-align: left;"><code>:make</code>, and if it compiled, run it; a key comes back</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:cn</code>, <code>:cp</code></td>
<td style="text-align: left;">the next, the previous compiler message</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:cc</code> <em>n</em>, <code>:cl</code></td>
<td style="text-align: left;">message <em>n</em>; the whole list</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>:set ts=</code><em>n</em></td>
<td style="text-align: left;">Tab’s width: it puts spaces to the next stop, never a tab character (4 unless told; <code>set ts=2</code> in VI.RC keeps it, for PROG too)</td>
</tr>
</tbody>
</table>

</div>

The command word is read in either case, so `:Q` and `:WQ` work with the caps lock on — which is how most people arrive in VI from a BASIC.

### Compiling from VI

`:make` is the edit–compile loop without leaving the editor. It saves the file, compiles it with the machine’s own `CC` or `PAS` ([Chapter 13, The Linux Underneath](13-linux.md)) — which one, the name decides — and puts the cursor on the first error, with the message on the status line:

    VI HELLO.C
    :make

`error 1 of 3: ’;’ expected` — and `:cn` is the next, `:cp` the one before, `:cl` all of them on one screen, `:cc 3` the third. Mad Pascal gives the column as well as the line, and the cursor goes to it. An error in another file — a unit, or `k4510.h` — is shown with that file’s name and does not move the cursor. A clean compile says what it made, `hello.prg: 2140 bytes`, and counts the warnings, which `:cn` walks through the same way.

`:run` is `:make` and then the program, as if typed at the prompt. It runs over VI by way of `SWAP`, so VI is there again when it ends; the program’s last screen stays up until you press a key. VI reads the file back afterwards, because a program may use the far memory VI keeps the text in — which is why `:run` saves first, and why the undo history starts again. From a VI that was itself started by `SWAP` (from a BASIC’s `*VI`, or RANGER), `:run` cannot nest and says so: leave VI and type the program’s name.

The compilers write what they said to `/SYSTEM/LOG/MAKE.ERR`, one message a line in one form for every language (`FILE:LINE:COL:KIND:TEXT`), and that file is all VI reads — so a compiler added later needs a script, not a new VI.

### Making `jk` leave insert mode

The usual mapping is the one most people want first, and it is typed at the `:` line exactly like this, with the Escape spelt out in angle brackets:

    :imap jk <Esc>

From then on, typing `j` then `k` in insert mode leaves it, and the two letters never reach the file. A lone `j` still does: the editor holds it for one second waiting for the `k` — vim’s own `timeoutlen`, and long enough for a deliberate “j, k” — and hands it over when nothing comes. `<CR>` is the other spelt-out key; everything else in the result is literal.

To have it every time, put the same line, without the colon, in `/SYSTEM/ETC/VI.RC`: `VI` runs that file once the text is loaded, one ex command per line, a line starting with `"` being a comment. `/SYSTEM/ETC/VI.SAMPLE` is one to copy, the way `STARTUP.SAMPLE` is, and the file is yours — it is not in the repository. If a mapping you have defined seems not to fire, the usual reason is that there is no `VI.RC` on that machine, only the sample.

!!! note ""
    **Where the file lives.** Nothing of it is in the 64 KB. Every line is a 256-byte slot in far memory — a length and up to 255 characters — and only the line under the cursor is brought down, fetched when the cursor arrives and written back when it leaves. So the ceiling is far memory rather than the address space: 32000 lines, and `LOAD` and `SAVE` go straight to and from a flat copy out there without the file ever passing through the CPU’s view. Undo is a journal in the same place, which is why it is unlimited: one level would have cost the same as all of them.
    
    A 1.28 MB file of 20000 lines — twenty times the whole address space — opens, edits at the far end and saves back byte for byte.

## PROG

`PROG name` is the front end for writing a program in C or Pascal: the text, a menu bar, and — under the text — what the compiler said about it. It is VI’s engine with modern keys on it, in the spirit of Turbo Pascal: F9 compiles, Ctrl-F9 compiles and runs, and an error puts the cursor on the line it is about.

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>Key</strong></th>
<th style="text-align: left;"><strong>Does</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;">arrows, Home, End, PgUp, PgDn</td>
<td style="text-align: left;">move; with Ctrl, a word at a time and the ends of the file</td>
</tr>
<tr class="even">
<td style="text-align: left;">Enter, Tab</td>
<td style="text-align: left;">a new line that keeps the indent; spaces to the next tab stop (four, or <code>set ts=</code> in VI.RC)</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Insert</td>
<td style="text-align: left;">insert or overwrite (the cursor is a bar or a block)</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl-S <em>or</em> F2, Ctrl-O, Ctrl-Q</td>
<td style="text-align: left;">save, open, quit — asking first about unsaved changes</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl-Z, Ctrl-Y</td>
<td style="text-align: left;">undo, redo, as far back as the session goes</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl-X, Ctrl-C, Ctrl-V</td>
<td style="text-align: left;">cut, copy, paste the line</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl-F, F3, Ctrl-R, Ctrl-G</td>
<td style="text-align: left;">find, find again, replace everywhere, go to a line</td>
</tr>
<tr class="even">
<td style="text-align: left;">F9, Ctrl-F9</td>
<td style="text-align: left;">compile the <code>.C</code> (<code>CC</code>) or <code>.PAS</code> (<code>PAS</code>); and run it</td>
</tr>
<tr class="odd">
<td style="text-align: left;">F4, Shift-F4</td>
<td style="text-align: left;">the next, the previous compiler message</td>
</tr>
<tr class="even">
<td style="text-align: left;">F10, F1</td>
<td style="text-align: left;">the menu; the keys</td>
</tr>
</tbody>
</table>

</div>

The messages are the ones `:make` reads in VI, from the same `/SYSTEM/LOG/MAKE.ERR`: an error in the file on screen takes the cursor to its line (and column, from Mad Pascal); one in another file — a unit, a header — is listed with that file’s name. A `}` typed on a line of its own goes back a level. F7 and F8 stay the machine’s (the menu, pause), so PROG leaves them alone; Ctrl-H is Backspace on this keyboard, which is why replace is Ctrl-R.

!!! note ""
    **Where it is going.** This is PROG’s first stage: one file at a time. Next come tabs — several files open, and a message about another file opening it — then projects (a `PROJECT.K4P` naming the sources, for a C program in several files), the mouse and selection, and last the interpreters: EhBASIC, Microsoft BASIC, LOGO and Forth run on the file in front of you, their errors in the same list.

## Editing from inside a BASIC

There are two cases, and they look alike, so here is the rule.

**To edit the program you are writing**, type `*EDIT` or `*VI` with nothing after it. BASIC saves the program to a temporary file, runs the editor on it, and loads it back when you leave — so what you type in the editor is what you `LIST` afterwards. Variables do not survive the round trip, exactly as with `LOAD`. [Editing the program in VI](04-ehbasic.md#editing-the-program-in-vi) has the details for EhBASIC; Microsoft BASIC does the same with `*VI` ([Chapter 5, Microsoft BASIC, 1977](05-msbasic.md)), BBC BASIC with `*VI` and `*EDIT` ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), and LOGO with `EDIT "name` ([Chapter 8, LOGO](08-logo.md)).

**To renumber it**, which none of those BASICs could do for itself, use `:renum` in VI or Ctrl-R in EDIT. Both know the language from the file: a `.BAS` is EhBASIC’s or Microsoft’s, a `.BBC` is BBC BASIC’s, with its `ELSE` targets and its lower-case variables (a `goto` there is a name, not a jump). The lines’ own numbers change, and so does every target after `GOTO`, `GOSUB`, `THEN`, `RESTORE` and `ON`…`GOTO`; strings, `REM` and `DATA` are left alone. A `GOTO` to a line that does not exist is kept as it is and counted in the message, and a program whose numbers are out of order is refused, not guessed at. LOGO has no line numbers, and says so.

**To edit any other file**, put `SWAP` in front:

    *SWAP EDIT NOTES.TXT

A program loaded from the shell lands on top of whatever is in memory, so a plain `*EDIT NOTES.TXT` would run the editor over the program you were writing. `SWAP` ([Chapter 2, The Shell](02-shell.md)) puts the machine away first — program, variables, screen and all — runs the editor on a clean one, and gives yours back when the editor exits.

So: no file name, no `SWAP` needed, it is done for you; a file name, `SWAP` first.
