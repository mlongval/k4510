# The Code Page

Every character the K4510 shows is one byte, and the code page says which character each of the 256 bytes is. It is IBM’s code page 437 — the PC’s, the one with the box drawing — with 26 of its places given to letters Western Europe needs and CP437 never had: the accented capitals of French, Italian and Spanish, `œ` and `Œ`, the euro, the section and pilcrow signs, German’s quotes, and `ø`. The places they took were CP437’s Greek and mathematical symbols and a few marks nobody used. The box drawing, the shades and blocks, and every accented letter CP437 already had are where they always were, so a screen drawn for CP437 still draws as it was drawn — unless it used one of the 26.

Like the register reference, this appendix is not written by hand: `doc/guide/mkcodepage.py` makes it from `core/codepage.h`, the one table the machine’s text runs through. JIM turning UTF-8 into bytes and back, the keyboard, the fonts and `k4510-remote` all read that table, so none of them can disagree with this page, or with each other.

## The page

Generated from `core/codepage.h`. The row is a byte’s high hex digit, the column its low one: `$82` is row 8, column 2. The 26 places that are not CP437’s are in bold. <span class="smallcaps">sp</span> is the space and <span class="smallcaps">nb</span> the no-break space; `$00` draws nothing.

<div class="center">

<div class="tabular">

ll & 0 & 1 & 2 & 3 & 4 & 5 & 6 & 7 & 8 & 9 & A & B & C & D & E & F  
0 & & ☺ & ☻ & ♥ & ♦ & ♣ & ♠ & • & ◘ & ○ & ◙ & ♂ & ♀ & ♪ & ♫ & ☼  
1 & ► & ◄ & ↕ & ‼ & ¶ & § & ▬ & ↨ & ↑ & ↓ & → & ← & ∟ & ↔ & ▲ & ▼  
2 & SP & ! & " & \# & \$ & % & & & ’ & ( & ) & \* & + & , & - & . & /  
3 & 0 & 1 & 2 & 3 & 4 & 5 & 6 & 7 & 8 & 9 & : & ; & \< & = & \> & ?  
4 & @ & A & B & C & D & E & F & G & H & I & J & K & L & M & N & O  
5 & P & Q & R & S & T & U & V & W & X & Y & Z & \[ & \\ & \] & ^ & \_  
6 & ‘ & a & b & c & d & e & f & g & h & i & j & k & l & m & n & o  
7 & p & q & r & s & t & u & v & w & x & y & z & { & | & } & ~ & ⌂  
8 & Ç & ü & é & â & ä & à & å & ç & ê & ë & è & ï & î & ì & Ä & Å  
9 & É & æ & Æ & ô & ö & ò & û & ù & ÿ & Ö & Ü & ¢ & £ & ¥ & **€** & **§**  
A & á & í & ó & ú & ñ & Ñ & ª & º & ¿ & **¶** & ¬ & ½ & ¼ & ¡ & « & »  
B & ░ & ▒ & ▓ & │ & ┤ & ╡ & ╢ & ╖ & ╕ & ╣ & ║ & ╗ & ╝ & ╜ & ╛ & ┐  
C & └ & ┴ & ┬ & ├ & ─ & ┼ & ╞ & ╟ & ╚ & ╔ & ╩ & ╦ & ╠ & ═ & ╬ & ╧  
D & ╨ & ╤ & ╥ & ╙ & ╘ & ╒ & ╓ & ╫ & ╪ & ┘ & ┌ & █ & ▄ & ▌ & ▐ & ▀  
E & **À** & ß & **Â** & **È** & **Ê** & **Ë** & µ & **Î** & **Ï** & **Ô** & **Ù** & **Û** & **Ÿ** & **Á** & **Í** & **Ó**  
F & **Ú** & ± & **Ì** & **Ò** & **œ** & **Œ** & ÷ & **ø** & ° & **Ø** & · & **„** & **“** & ² & ■ & NB  

</div>

</div>

### The 26 places

<div class="center">

<table>
<thead>
<tr class="header">
<th style="text-align: left;">Byte</th>
<th colspan="2" style="text-align: left;">The K4510 page</th>
<th style="text-align: left;">CP437 had</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><code>$9E</code></td>
<td style="text-align: left;"><code>€</code></td>
<td style="text-align: left;">euro sign</td>
<td style="text-align: left;"><code>₧</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$9F</code></td>
<td style="text-align: left;"><code>§</code></td>
<td style="text-align: left;">section sign</td>
<td style="text-align: left;"><code>ƒ</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$A9</code></td>
<td style="text-align: left;"><code>¶</code></td>
<td style="text-align: left;">pilcrow sign</td>
<td style="text-align: left;"><code>⌐</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$E0</code></td>
<td style="text-align: left;"><code>À</code></td>
<td style="text-align: left;">latin capital letter a with grave</td>
<td style="text-align: left;"><code>α</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$E2</code></td>
<td style="text-align: left;"><code>Â</code></td>
<td style="text-align: left;">latin capital letter a with circumflex</td>
<td style="text-align: left;"><code>Γ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$E3</code></td>
<td style="text-align: left;"><code>È</code></td>
<td style="text-align: left;">latin capital letter e with grave</td>
<td style="text-align: left;"><code>π</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$E4</code></td>
<td style="text-align: left;"><code>Ê</code></td>
<td style="text-align: left;">latin capital letter e with circumflex</td>
<td style="text-align: left;"><code>Σ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$E5</code></td>
<td style="text-align: left;"><code>Ë</code></td>
<td style="text-align: left;">latin capital letter e with diaeresis</td>
<td style="text-align: left;"><code>σ</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$E7</code></td>
<td style="text-align: left;"><code>Î</code></td>
<td style="text-align: left;">latin capital letter i with circumflex</td>
<td style="text-align: left;"><code>τ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$E8</code></td>
<td style="text-align: left;"><code>Ï</code></td>
<td style="text-align: left;">latin capital letter i with diaeresis</td>
<td style="text-align: left;"><code>Φ</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$E9</code></td>
<td style="text-align: left;"><code>Ô</code></td>
<td style="text-align: left;">latin capital letter o with circumflex</td>
<td style="text-align: left;"><code>Θ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$EA</code></td>
<td style="text-align: left;"><code>Ù</code></td>
<td style="text-align: left;">latin capital letter u with grave</td>
<td style="text-align: left;"><code>Ω</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$EB</code></td>
<td style="text-align: left;"><code>Û</code></td>
<td style="text-align: left;">latin capital letter u with circumflex</td>
<td style="text-align: left;"><code>δ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$EC</code></td>
<td style="text-align: left;"><code>Ÿ</code></td>
<td style="text-align: left;">latin capital letter y with diaeresis</td>
<td style="text-align: left;"><code>∞</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$ED</code></td>
<td style="text-align: left;"><code>Á</code></td>
<td style="text-align: left;">latin capital letter a with acute</td>
<td style="text-align: left;"><code>φ</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$EE</code></td>
<td style="text-align: left;"><code>Í</code></td>
<td style="text-align: left;">latin capital letter i with acute</td>
<td style="text-align: left;"><code>ε</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$EF</code></td>
<td style="text-align: left;"><code>Ó</code></td>
<td style="text-align: left;">latin capital letter o with acute</td>
<td style="text-align: left;"><code>∩</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$F0</code></td>
<td style="text-align: left;"><code>Ú</code></td>
<td style="text-align: left;">latin capital letter u with acute</td>
<td style="text-align: left;"><code>≡</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$F2</code></td>
<td style="text-align: left;"><code>Ì</code></td>
<td style="text-align: left;">latin capital letter i with grave</td>
<td style="text-align: left;"><code>≥</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$F3</code></td>
<td style="text-align: left;"><code>Ò</code></td>
<td style="text-align: left;">latin capital letter o with grave</td>
<td style="text-align: left;"><code>≤</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$F4</code></td>
<td style="text-align: left;"><code>œ</code></td>
<td style="text-align: left;">latin small ligature oe</td>
<td style="text-align: left;"><code>⌠</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$F5</code></td>
<td style="text-align: left;"><code>Œ</code></td>
<td style="text-align: left;">latin capital ligature oe</td>
<td style="text-align: left;"><code>⌡</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$F7</code></td>
<td style="text-align: left;"><code>ø</code></td>
<td style="text-align: left;">latin small letter o with stroke</td>
<td style="text-align: left;"><code>≈</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$F9</code></td>
<td style="text-align: left;"><code>Ø</code></td>
<td style="text-align: left;">latin capital letter o with stroke</td>
<td style="text-align: left;"><code>∙</code></td>
</tr>
<tr class="odd">
<td style="text-align: left;"><code>$FB</code></td>
<td style="text-align: left;"><code>„</code></td>
<td style="text-align: left;">double low-9 quotation mark</td>
<td style="text-align: left;"><code>√</code></td>
</tr>
<tr class="even">
<td style="text-align: left;"><code>$FC</code></td>
<td style="text-align: left;"><code>“</code></td>
<td style="text-align: left;">left double quotation mark</td>
<td style="text-align: left;"><code>ⁿ</code></td>
</tr>
</tbody>
</table>

</div>

## The languages it writes

With the 26, these are complete in both cases: French, Italian, Spanish, German, Danish, Norwegian, Swedish, Finnish, Dutch, Catalan, Galician, Basque, Irish, Scottish Gaelic, Afrikaans and Albanian — and English. Portuguese is four letters short (`ã õ Ã Õ`); the capital sharp `ẞ` is left out as too rare to spend a place on. Text arriving as UTF-8 — from `!`, from TELNET, over ssh — is turned into the page’s bytes on the way in, and the curly quotes the page has no place for become their plain forms.

## The first 32, and \$7F

Bytes `$01`–`$1F` and `$7F` draw CP437’s little pictures: the faces, the card suits, the arrows, the notes. As text they are also the control codes — `$07` rings, `$1B` is Escape, `$18` to `$1A` move the cursor — and the console obeys them rather than drawing them. So a program that wants a picture on the screen puts the byte into screen memory itself, as the games do, and the characters that matter as text (`§`, `¶`) have places in the upper half as well.

## Typing them

Every character a keyboard layout has a key for reaches the machine. The dead keys compose the rest: `‘` then `A` is `À`, `^` then `E` is `Ê` — 55 compositions in all, the capitals included. FONTED redraws any of the 256, in both sizes of the font, and shows the Unicode value of the character it is editing from the same table as this page.

## A BBS

Most ANSI art on a bulletin board is CP437, and nearly all of it is box drawing and shades, which the page kept. If it used `≡` or `√`, though, the K4510 page would draw the letter that lives in that place now — so TELNET does not use it for one. The machine keeps the same font a second time in strict CP437, and TELNET draws with that for a CP437 session: a BBS, an old system, a far end that never negotiates. A Unix host, which takes the XTERM-COLOR terminal type and talks UTF-8, is drawn in the K4510 page, and the machine’s font comes back when TELNET hangs up.
