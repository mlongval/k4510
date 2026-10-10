# The Editors

Two of them, because they answer different questions. `EDIT` is the one to reach for first: it looks and works like the editor MS-DOS shipped from version 5 on, menus, mouse and all, so there is nothing to learn. `VI` is for the file too big to think about, and it is modal, so it expects you to have met `vi` before. Under both is the same engine — and `PROG` (below) is the third front end on it. `WORD` and `CALC`, further down, are not editors — a reader and a spreadsheet — but they wear the same clothes, so they are kept here with them.

All of them take their keys raw from the ROM — an arrow arrives as one byte rather than an escape sequence to unpick, with a bit beside it saying that it *is* an arrow and not the accented letter that shares its code. None needs the Tube: they are programs of this machine.

## EDIT

`EDIT name` opens a file, or starts a new one by that name; `EDIT` alone starts an untitled one. The screen is MS-DOS 5’s: the menu bar along the top, the text in a framed window with the file’s name in its top border, a scroll bar down the right and another along the foot, and the status line at the bottom with the line and column. The colours are EDIT’s too — grey menus, the text on blue, the status line in cyan. `EDIT -s name` uses the console’s own colours instead, and the *Options* menu switches between the two while you work. *Options*, *Tab Width…* sets how many spaces Tab puts, from 1 to 16 (four unless told): Tab always puts spaces up to the next stop and never a tab character, so a file looks the same in every editor. PROG has the same menu item, and the two (and VI’s `:set ts=`) share one setting.

The text lives in far memory, not in the 64 KB: a file may run to 16 384 lines of up to 255 characters, and undo goes back as far as the session does. Files are saved with a newline at the end of each line; a DOS file’s carriage returns are read and dropped.

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
<td style="text-align: left;">Shift with a moving key, Ctrl+A</td>
<td style="text-align: left;">select; typing, Enter, Backspace and Del replace what is selected</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+X, Ctrl+C, Ctrl+V</td>
<td style="text-align: left;">cut, copy, paste — DOS’s Shift+Del, Ctrl+Ins and Shift+Ins too. With nothing selected, the line</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+Z, Ctrl+Y</td>
<td style="text-align: left;">undo, redo</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Insert</td>
<td style="text-align: left;">insert or overwrite (an underline cursor or a block)</td>
</tr>
<tr class="even">
<td style="text-align: left;">Enter, Tab</td>
<td style="text-align: left;">a new line that keeps the indent; spaces to the next tab stop — Tab and Shift+Tab indent and outdent a selection</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Q</td>
<td style="text-align: left;">a new file, open, save, leave — asking first about unsaved changes</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+F, F3, Ctrl+G</td>
<td style="text-align: left;">find, find again, go to a line</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+R</td>
<td style="text-align: left;">renumber a BASIC program: 10, 20, 30…, and every <code>GOTO</code> with it</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+U</td>
<td style="text-align: left;">BBC BASIC’s keywords in capitals (<code>print</code> becomes <code>PRINT</code>); Ctrl+Z puts them back</td>
</tr>
<tr class="odd">
<td style="text-align: left;">F10, or Alt and a letter</td>
<td style="text-align: left;">the menus: Alt+F is <em>File</em>, Alt+S <em>Search</em>; then the arrows, Enter, or an entry’s letter; Esc closes</td>
</tr>
<tr class="even">
<td style="text-align: left;">F1</td>
<td style="text-align: left;">the keys</td>
</tr>
</tbody>
</table>

</div>

**The mouse** does what it did in DOS. A click puts the cursor in the text, a drag selects, Shift and a click selects from the cursor, and the wheel scrolls. A click on a menu’s name opens it and a click on an entry runs it. The scroll bars take clicks too: the arrows move a line or a column, the bar on either side of its thumb a page, and the thumb can be dragged.

**The dialogs** — *Open*, *Save As*, *Find*, *Change*, *Go To Line* — are boxes with a shadow, in the DOS way: Tab goes from one field or button to the next, Enter is the first button, Esc cancels, and the mouse clicks any of them. *Open* lists the directory you are in; Enter or a second click on a directory goes into it (and the shell’s current directory goes with it, as DOS’s did), on a file opens it. *Change* replaces every match in the file at once, and one Ctrl+Z takes all of it back.

**VI’s keys**, for the fingers that have them: *Options*, *VI Keys*, or `EDIT -v name`. EDIT then starts in VI’s normal mode, and everything in the VI section below about moving and changing holds — it is the same code, not a copy of it: counts, `hjkl`, `w b e`, `d c y` with a motion or doubled, `x p u`, Ctrl-R; `.` is the one thing missing. `i a o` and the rest type until Esc, with EDIT’s keys while you do (Shift and an arrow still selects). `:` `/` and `?` open a line on the status row: `:w` `:q` `:q!` `:wq` `:x`, `:12` goes to line 12, `:%s/old/new/g`. The menus, the mouse and the dialogs stay as they were, and so do the Ctrl keys — Ctrl+S saves, Ctrl+Q leaves — except Ctrl-R (redo), Ctrl-U and Ctrl-D (half a page), which are VI’s in normal mode. What you yank or delete is the clipboard, so `dw` then Ctrl+V works, and Ctrl+C then `p`. The maps come too: `:map` and `:imap` on the `:` line, and VI.RC’s at the start — so `imap jk <Esc>` there (it ships that way) is `jk` out of insert mode in VI, EDIT and PROG alike (a lone `j` is typed after a second, as vim does).

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
<td style="text-align: left;">a screenful; Ctrl-D, Ctrl-U half of one</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Enter, <code>+</code>, <code>-</code></td>
<td style="text-align: left;">the next line, the previous one, at its first non-blank</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>/</code><em>text</em></td>
<td style="text-align: left;">to the next <em>text</em> — a plain substring, not a pattern</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>?</code><em>text</em></td>
<td style="text-align: left;">to the previous one</td>
</tr>
<tr class="even">
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
<td style="text-align: left;"><code>:set wrap</code></td>
<td style="text-align: left;">a line longer than the screen is folded onto the rows under it (so it starts); <code>:set nowrap</code> keeps it to one row that scrolls sideways, and <code>:set wrap!</code> changes it over. Folded, <code>j</code> and <code>k</code> still move by a line of the file, and <code>gj</code> and <code>gk</code> by a row of the screen. <code>set nowrap</code> in VI.RC keeps it</td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>:set ts=</code><em>n</em></td>
<td style="text-align: left;">Tab’s width: it puts spaces to the next stop, never a tab character (4 unless told; <code>set ts=2</code> in VI.RC keeps it, for PROG too)</td>
</tr>
</tbody>
</table>

</div>

The command word is read in either case, so `:Q` and `:WQ` work with the caps lock on — which is how most people arrive in VI from a BASIC.

### Compiling from VI

`:make` is the edit–compile loop without leaving the editor. It saves the file, compiles it with the machine’s own `CC` or `PAS` ([Chapter 12, The Linux Underneath](13-linux.md)) — which one, the name decides — and puts the cursor on the first error, with the message on the status line:

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

`PROG name` is the front end for writing a program in C, Pascal or REXX. It looks and works like `EDIT` — the same menus, dialogs, scroll bars, mouse and colours (`PROG -s` for the console’s own) — with the open files as tabs along the window’s top border and, under the text, a second window with what the compiler said. In the spirit of Turbo Pascal: F9 compiles, Ctrl+F9 compiles and runs, and an error puts the cursor on the line it is about. `PROG -v` (or *Options*, *VI Keys*) has VI’s keys exactly as `EDIT -v` has them, and four more on the `:` line: `:make` `:run` `:cn` `:cp` are F9, Ctrl+F9, F4 and Shift+F4; `:q` asks about every changed file, as Ctrl+Q does, and `:q!` leaves without asking.

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
<td style="text-align: left;">moving, selecting, the clipboard, undo</td>
<td style="text-align: left;">as in <code>EDIT</code> (above): the arrows with Ctrl and Shift, Ctrl+A, Ctrl+X/C/V, Ctrl+Z/Y</td>
</tr>
<tr class="even">
<td style="text-align: left;">Enter, Tab</td>
<td style="text-align: left;">a new line that keeps the indent; spaces to the next tab stop (four, or <em>Options</em>, <em>Tab Width</em>, or <code>set ts=</code> in VI.RC) — with lines selected, Tab and Shift+Tab indent them and take the indent back</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Insert</td>
<td style="text-align: left;">insert or overwrite (an underline cursor or a block)</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+S <em>or</em> F2, Ctrl+O, Ctrl+Q</td>
<td style="text-align: left;">save, open (in a tab of its own), leave — asking first about every file with unsaved changes</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+N, Ctrl+W, F6, Shift+F6</td>
<td style="text-align: left;">a new file, close this one, the next and the previous file</td>
</tr>
<tr class="even">
<td style="text-align: left;"><em>File</em> → <em>New Project</em></td>
<td style="text-align: left;">a folder with a <code>PROJECT.K4P</code> and a first file that compiles as it stands</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+F, F3, Ctrl+R, Ctrl+G</td>
<td style="text-align: left;">find, find again, change (every match), go to a line</td>
</tr>
<tr class="even">
<td style="text-align: left;">Shift+Ctrl+F</td>
<td style="text-align: left;">find in files: every source file in this file’s directory, into the message list</td>
</tr>
<tr class="odd">
<td style="text-align: left;">F9, Ctrl+F9</td>
<td style="text-align: left;">save every changed file, compile the <code>.C</code> (<code>CC</code>) or <code>.PAS</code> (<code>PAS</code>); and run it. A <code>.RX</code> has nothing to compile: F9 saves it, Ctrl+F9 runs it, and an error takes you to its line</td>
</tr>
<tr class="even">
<td style="text-align: left;">F4, Shift+F4</td>
<td style="text-align: left;">the next, the previous message — one about another file opens it</td>
</tr>
<tr class="odd">
<td style="text-align: left;">F10, or Alt and a letter</td>
<td style="text-align: left;">the menus: Alt+B is <em>Build</em>; F1 the keys</td>
</tr>
<tr class="even">
<td style="text-align: left;">the mouse</td>
<td style="text-align: left;">the text, a menu, a tab, a message, the scroll bars; a drag selects, the wheel scrolls</td>
</tr>
</tbody>
</table>

</div>

`PROG` does not renumber BASIC: that is `EDIT`’s Ctrl+R and VI’s `:renum`, and since PROG took `EDIT`’s clothes there is no room left in it for a third copy.

The messages are the ones `:make` reads in VI, from the same `/SYSTEM/LOG/MAKE.ERR`: an error in the file on screen takes the cursor to its line (and column, from Mad Pascal); one in another file — a unit, a header — is listed with that file’s name. A `}` typed on a line of its own goes back a level. F12 stays the machine’s (its menu; Shift+F12 pauses), so PROG leaves it alone; Ctrl-H is Backspace on this keyboard, which is why change is Ctrl+R.

Up to eight files are open at once, as tabs in the window’s top border — the one in front lit, a `*` on each with unsaved changes; a click on a tab brings it forward. F9 saves every changed file before it compiles, because the compilers read the disk: a unit edited in another tab is what `PAS` sees. An error in another file — a unit, a header — opens that file (or brings its tab forward) at the line. Find in files (Shift+Ctrl+F, or *Search*) looks through every source file in the directory of the file in front, and what it finds goes into the message list, where F4 walks it like errors. After Ctrl-F9 the other open files are read back from the disk: a program may use the memory they wait in.

### Projects

A program in several files is a *project*: a folder, and in it a `PROJECT.K4P` that names them, one `KEY=value` to a line:

    # GAME -- a PROG project
    NAME=GAME
    LANG=C
    SRC=GAME.C SPRITES.C SOUND.C

For Pascal it is `MAIN=GAME.PAS` instead of `SRC=` — the units come in by `uses`, from the same folder — and `OUT=` names the program when `NAME` in lower case will not do. With a `PROJECT.K4P` beside the file in front, F9 builds the project, whichever of its files you are in — a header or a unit too — and Ctrl-F9 runs the project’s program; the files row starts with its name in brackets. `PROG GAME/PROJECT.K4P` (or *Open* on it) opens every source in a tab, and *File* → *New project* makes the folder, the project file and a first file that compiles as it stands. `CC -p` and `PAS -p` build a project from the prompt the same way.

A C project compiles each source once and keeps what it made (on the Linux side, never in your folder): the next F9 compiles only the files that changed — all of them if a `.H` beside them did — and says so, `game.prg: 4211 bytes (1 compiled, 2 kept)`.

!!! note ""
    **Where it is going.** This is PROG’s fourth stage: projects came third, the mouse and selection fourth, and REXX is already in. Next come the other interpreters: EhBASIC, LOGO and Forth run on the file in front of you, their errors in the same list.

## WORD

`WORD name` reads a Microsoft Word document — a `.DOCX`, what Word has saved since 2007 — in the same clothes as `EDIT`: the menu bar, the frame with the name in it, the scroll bar, the status line with where you are and how many words there are. It reads; it does not write a `.DOCX`. *File* → *Save As Text* (Ctrl+S) writes what it shows as a plain text file, a paragraph to a line, which `EDIT` can then take.

The page is laid out to the window. Each paragraph is wrapped at a word, centred or set right where Word had it so, with a blank line between paragraphs; lists keep their bullets, and numbered lists their numbers; a table comes out a row to a line, with its cells between bars. A text cell has a colour and nothing else, so emphasis is colour: **bold** is white, *italic* cyan, both yellow, underlined green, a link light blue; the title and the first headings yellow, the next white, then cyan. Accented letters come through as far as the machine’s code page has them — *é* and *ç* do; most accented capitals lose the accent.

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
<td style="text-align: left;">Up, Down, PgUp, PgDn, Space</td>
<td style="text-align: left;">a line, a screen</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+Home, Ctrl+End</td>
<td style="text-align: left;">the beginning, the end</td>
</tr>
<tr class="odd">
<td style="text-align: left;">Ctrl+F, F3</td>
<td style="text-align: left;">find (in any case), find again</td>
</tr>
<tr class="even">
<td style="text-align: left;">Ctrl+O, Ctrl+S, Ctrl+Q <em>or</em> Esc</td>
<td style="text-align: left;">open, save as text, leave</td>
</tr>
<tr class="odd">
<td style="text-align: left;">F10, Alt and a letter</td>
<td style="text-align: left;">the menus; F1 the keys; the mouse works the menus, the scroll bar and the wheel</td>
</tr>
</tbody>
</table>

</div>

`WORD -s name` is in the console’s own colours, as `EDIT -s`. *View*, *Tab Width…* chooses how many spaces a Tab in the document is drawn as (four unless told, 1 to 16; always spaces, never a tab character), and reads the file again so the change shows.

**How it reads one.** A `.DOCX` is a zip of XML files, and the machine can already `MOUNT` a zip ([Chapter 2, The Shell](02-shell.md)): WORD mounts the document, reads `word/document.xml` — and the styles and list definitions beside it — into far memory, unmounts it, and parses the XML itself, on the 45GS10. Pictures, text boxes, footnotes, headers and footers, fonts and sizes are passed over. A Word 97–2003 `.DOC` is another kind of file altogether and WORD says so rather than guessing; save it as `.DOCX` in Word first. A `.TXT` opens too.

## CALC

`CALC name` is the spreadsheet, in the same clothes as `EDIT` and `WORD`: the menu bar, the frame with the sheet’s name in it, the scroll bars, the status line. Under the frame’s top sits the formula bar — the cell’s name, what is in it, and what a formula comes to at the right — then the column letters, the rows, 52 columns (A–Z, AA–AZ) by 999 rows. `CALC` alone starts an empty sheet; `CALC -s name` is in the console’s own colours, as `EDIT -s`.

Type into a cell and it is taken for what it looks like: a number is a number, an `=` starts a formula, anything else is text, and a leading `'` forces text (`'2026` stays a year). Enter keeps the entry and goes down, Tab keeps it and goes right, and Enter after a run of Tabs goes back to the column the run began in, as Excel does. F2 edits the cell in place (the arrows then move inside the entry); Esc drops the entry. Text longer than its column runs on into the empty cells beside it; a number wider than its column shows `#####`.

The spelling is Excel’s and LibreOffice Calc’s, which is what anyone brings to a spreadsheet now:

- references `A1`, ranges `A1:B9`, and absolute or mixed references `$A$1`, `A$1`, `$A1`. While a formula is being typed, F4 on a reference cycles it through the four forms.
- `+ - * / ^`, the comparisons `= <> < > <= >=` (which give `TRUE` or `FALSE`), and `&` to join text; text itself goes in double quotes: `="Total: "&A1`.
- the functions: `SUM AVERAGE MIN MAX COUNT COUNTA` over ranges and lists; `ABS INT SQRT ROUND(x;n) MOD PI()`; `IF(test;then;else) AND OR NOT ISERROR IFERROR NA()`; `LEN LEFT RIGHT MID UPPER LOWER TRIM CONCAT VALUE`; `TODAY()` and `NOW()`, which count days from 1899-12-30 as Excel does (the machine’s clock; the MATH unit’s seven digits put `NOW` within a few minutes).
- the lookups, as Excel has them: `VLOOKUP(value;range;col;[sorted])` looks down the range’s first column and answers from column `col` of the row it finds, `HLOOKUP` the same along the first row; `MATCH(value;range;[type])` gives the place in a row or a column (type 1, the default, the largest not past the value in a rising list; 0 exact; −1 the smallest not below it in a falling one), and `INDEX(range;row;[col])` the cell at that place. With `FALSE` or 0 a lookup wants the value exactly (text in any case); with `TRUE` or nothing the column is taken as sorted and the largest value not past the one sought is the match. Not found is `#N/A`, a column past the range `#REF!`, a column below 1 `#VALUE!`.
- arguments part with a comma or a semicolon — Excel’s comma, LibreOffice’s semicolon in French — and a formula is kept and shown as it was typed.
- the error values: `#DIV/0!`, `#REF!` (a cell off the sheet, or one a paste lost), `#NAME?`, `#VALUE!`, `#N/A`, `#NUM!`, and `#ERROR!` for a formula that will not parse. An error in a cell reaches every formula that uses it.

The sheet is worked out again after every entry, in the order its formulas need: a formula above the cells it uses is simply left for the next pass, and what is still waiting when a pass can do nothing more is a circle — those cells show `#CIRC!` instead of the sheet hanging. F9 works it out again by hand (for `NOW()`).

Shift with an arrow selects a range. Ctrl+X, Ctrl+C and Ctrl+V cut, copy and paste the cell or the range, and a pasted formula’s references move with it — `=A1*2` copied two rows down reads `A3` — while a `$` holds its half still. Ctrl+D fills down and Ctrl+R fills right, from the first row or column of the selection (a lone cell takes from the cell above it, or to its left). Del clears.

Ctrl+Z undoes and Ctrl+Y redoes, as in Excel and LibreOffice (*Edit* → *Undo*, *Redo*): an entry, a clear, a cut, a paste, a fill, a format from the *Cells* menu and a column width, the whole range at once, and the cursor goes to what changed. The history keeps the last hundred steps, or 4 MB of them, the oldest going first; doing something new after an undo ends what Redo had, and *New*, *Open* and *Import CSV* start it afresh. Undo and the lookups are not in CALC’s main memory at all: they are an overlay that the far-call gate swaps in at $E000 when one of them is called (EDIT keeps VI’s keys the same way).

The *Cells* menu formats the cell or the range: *General*, *Decimals…* (0 to 9 places), *Thousands* (on or off; a space between them, `1 234 567.00`, the point for the decimals), *Percent*, *Date* (which shows a day count as `2026-10-10` and widens the column to hold it); and *Column Width…* (Ctrl+W) or *All Columns…*. It is called *Cells* and not *Format* because a menu opens by its first letter, and *File* has the F.

| Key | Does |
|---|---|
| arrows, PgUp, PgDn, Home, End | move; Ctrl+Home A1, Ctrl+End the last cell used; Shift with any of them selects |
| Enter, Tab, Esc | keep and go down, keep and go right, drop the entry |
| F2, F4 | edit the cell in place; `$` on the reference under the cursor |
| Del, Ctrl+X, Ctrl+C, Ctrl+V | clear, cut, copy, paste |
| Ctrl+D, Ctrl+R | fill down, fill right |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+G, Ctrl+W, F9 | go to a cell, the column’s width, recalculate |
| Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Q *or* Esc | new, open, save, leave |
| F1, Shift+F1 | the keys, the formulas |
| F10, Alt and a letter | the menus; the mouse works the menus, the cells, the scroll bars and the wheel |

**Files.** A sheet is saved as text, a line a cell — `A1:F:=B2*3` — so `EDIT` can read one. The header says `K4CALC 3`; a format follows the kind letter (`A2:N2,:1234.5` is two decimals with thousands, `%` percent, `D` a date) and `W:B:12` lines carry the column widths. Sheets from the earlier CALCs still open: `K4CALC 2` as it is, and `K4CALC 1` — the first CALC’s VisiCalc spelling, `@SUM` and `A1...B3` — brought over as it loads, so the next save is written in the new spelling. *File* → *Import CSV* and *Export CSV* move a sheet to and from LibreOffice and Excel (`CALC name.CSV` imports too). Coming in, the separator is told from the file — comma, semicolon or Tab — quotes are undone and each field is taken for what it looks like; going out, values are written and not formulas, as those two do, text is quoted where it must be, and the separator is a comma unless *Options*, *CSV Semicolons* is on. A number with a decimal comma (`3,5` in a French file) comes in as text: the formulas here use the point.

The arithmetic is the MATH unit’s: IEEE single precision, seven digits.

## Editing from inside a BASIC

There are two cases, and they look alike, so here is the rule.

**To edit the program you are writing**, type `*EDIT` or `*VI` with nothing after it. BASIC saves the program to a temporary file, runs the editor on it, and loads it back when you leave — so what you type in the editor is what you `LIST` afterwards. Variables do not survive the round trip, exactly as with `LOAD`. BBC BASIC does the same with `*VI` and `*EDIT` ([Chapter 6, The Tube](06-tube.md)), and LOGO with `EDIT "name` ([Chapter 8, LOGO](08-logo.md)).

**To renumber it**, which none of those BASICs could do for itself, use `:renum` in VI or Ctrl-R in EDIT. Both know the language from the file: a `.BAS` is EhBASIC’s, a `.BBC` is BBC BASIC’s, with its `ELSE` targets and its lower-case variables (a `goto` there is a name, not a jump). The lines’ own numbers change, and so does every target after `GOTO`, `GOSUB`, `THEN`, `RESTORE` and `ON`…`GOTO`; strings, `REM` and `DATA` are left alone. A `GOTO` to a line that does not exist is kept as it is and counted in the message, and a program whose numbers are out of order is refused, not guessed at. LOGO has no line numbers, and says so.

**BBC BASIC wants its keywords in capitals**: to it `print` is a variable and only `PRINT` prints, so a program typed in lower case stops with *Mistake*. EDIT’s Ctrl+U (*Edit*, *Uppercase Keywords*) puts every keyword in the file in capitals — only a whole word that is one: `left(` becomes |LEFT(| and `procdraw` `PROCdraw`, but `total`, `count%` and `name$` stay the variables they are, and strings, the rest of a `REM` or `DATA` and a star command are left as typed. `EDIT -u name` does it at every save, so you can type in lower case throughout; BBC’s `*EDIT` starts EDIT that way. (A keyword run into a name, as in `fori%=1to10`, is not found: leave the spaces in. And BBC BASIC’s own `*LOWERCASE ON` is the other way round the problem: it takes the keywords in lower case, at the price of every lower-case variable that happens to be one.)

**To edit any other file**, put `SWAP` in front:

    *SWAP EDIT NOTES.TXT

A program loaded from the shell lands on top of whatever is in memory, so a plain `*EDIT NOTES.TXT` would run the editor over the program you were writing. `SWAP` ([Chapter 2, The Shell](02-shell.md)) puts the machine away first — program, variables, screen and all — runs the editor on a clean one, and gives yours back when the editor exits.

So: no file name, no `SWAP` needed, it is done for you; a file name, `SWAP` first.
