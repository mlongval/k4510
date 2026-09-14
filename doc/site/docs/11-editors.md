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
</tbody>
</table>

</div>

Accented letters type as themselves. The bar along the bottom carries the name, a `*` while there are unsaved changes, where the cursor is, and those last two keys, so there is nothing to remember. Long lines slide sideways rather than wrap.

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
</tbody>
</table>

</div>

The command word is read in either case, so `:Q` and `:WQ` work with the caps lock on — which is how most people arrive in VI from a BASIC.

### Making `jk` leave insert mode

The usual mapping is the one most people want first, and it is typed at the `:` line exactly like this, with the Escape spelt out in angle brackets:

    :imap jk <Esc>

From then on, typing `j` then `k` in insert mode leaves it, and the two letters never reach the file. A lone `j` still does: the editor holds it for one second waiting for the `k` — vim’s own `timeoutlen`, and long enough for a deliberate “j, k” — and hands it over when nothing comes. `<CR>` is the other spelt-out key; everything else in the result is literal.

To have it every time, put the same line, without the colon, in `/SYSTEM/ETC/VI.RC`: `VI` runs that file once the text is loaded, one ex command per line, a line starting with `"` being a comment. `/SYSTEM/ETC/VI.SAMPLE` is one to copy, the way `STARTUP.SAMPLE` is, and the file is yours — it is not in the repository. If a mapping you have defined seems not to fire, the usual reason is that there is no `VI.RC` on that machine, only the sample.

!!! note ""
    **Where the file lives.** Nothing of it is in the 64 KB. Every line is a 256-byte slot in far memory — a length and up to 255 characters — and only the line under the cursor is brought down, fetched when the cursor arrives and written back when it leaves. So the ceiling is far memory rather than the address space: 32000 lines, and `LOAD` and `SAVE` go straight to and from a flat copy out there without the file ever passing through the CPU’s view. Undo is a journal in the same place, which is why it is unlimited: one level would have cost the same as all of them.
    
    A 1.28 MB file of 20000 lines — twenty times the whole address space — opens, edits at the far end and saves back byte for byte.

## Editing from inside a BASIC

There are two cases, and they look alike, so here is the rule.

**To edit the program you are writing**, type `*EDIT` or `*VI` with nothing after it. BASIC saves the program to a temporary file, runs the editor on it, and loads it back when you leave — so what you type in the editor is what you `LIST` afterwards. Variables do not survive the round trip, exactly as with `LOAD`. [Editing the program in VI](04-ehbasic.md#editing-the-program-in-vi) has the details for EhBASIC; Microsoft BASIC does the same with `*VI` ([Chapter 5, Microsoft BASIC, 1977](05-msbasic.md)), BBC BASIC with `*VI` and `*EDIT` ([Chapter 6, The Tube: BBC BASIC](06-tube.md)), and LOGO with `EDIT "name` ([Chapter 8, LOGO](08-logo.md)).

**To edit any other file**, put `SWAP` in front:

    *SWAP EDIT NOTES.TXT

A program loaded from the shell lands on top of whatever is in memory, so a plain `*EDIT NOTES.TXT` would run the editor over the program you were writing. `SWAP` ([Chapter 2, The Shell](02-shell.md)) puts the machine away first — program, variables, screen and all — runs the editor on a clean one, and gives yours back when the editor exits.

So: no file name, no `SWAP` needed, it is done for you; a file name, `SWAP` first.
