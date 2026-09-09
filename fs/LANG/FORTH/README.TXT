FORTH on the K4510
----------------------
Type FORTH at the shell. This is Tali Forth 2 (public domain), running
as native 45GS10 code -- no Tube, no tricks: a .prg loaded at $4000.

  2 3 + .              arithmetic; . prints, u. prints unsigned
  : cube dup dup * * ; define words
  words                the dictionary (~31.7 KB free for yours)
  hex  decimal         change the number base
  $D000 c@             every chip register is a phone call away
  4000 20 disasm       the disassembler (a ? is a 45GS10 opcode
                       the strict-65c02 disassembler does not know)
  assembler-wordlist >order
  here 2 lda.# ...     the interactive assembler
  see cube             show a word's code
  bye                  back to the K/OS shell, screen and all

No blocks, no ed: files come later, through the fs device at $D300.
