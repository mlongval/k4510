" tools/nvim/syntax/bbcbasic.vim -- BBC BASIC, as the machine's Tube runs it
" (Richard Russell's BBC BASIC; .BBC files).  Its keywords are upper case and
" never inside a name, so case matters here (goto is a fine variable).
if exists("b:current_syntax") | finish | endif
syn case match
syn iskeyword @,48-57,_,$,%,#

syn keyword bbcStatement AUTO BPUT CALL CASE CHAIN CIRCLE CLEAR CLG CLOSE CLS COLOR COLOUR
syn keyword bbcStatement DATA DEF DIM DRAW ELLIPSE ELSE END ENDCASE ENDIF ENDPROC ENDWHILE
syn keyword bbcStatement ENVELOPE ERROR EXIT FILL FOR GCOL GOSUB GOTO IF INPUT INSTALL LET
syn keyword bbcStatement LINE LOCAL MODE MOUSE MOVE NEXT OF OFF ON ORIGIN OSCLI OTHERWISE
syn keyword bbcStatement PLOT PRINT PRIVATE PROC QUIT READ RECTANGLE REPEAT REPORT RESTORE
syn keyword bbcStatement RETURN RUN SOUND STEP STOP SWAP SYS THEN TINT TO TRACE UNTIL VDU
syn keyword bbcStatement WAIT WHEN WHILE WIDTH
syn keyword bbcFunction ABS ACS ADVAL ASC ASN ATN BGET BY CHR$ COS COUNT DEG DIV EOF EOR
syn keyword bbcFunction ERL ERR EVAL EXP EXT FN GET GET$ HIMEM INKEY INKEY$ INSTR INT
syn keyword bbcFunction LEFT$ LEN LN LOG LOMEM MID$ MOD NOT OPENIN OPENOUT OPENUP OR PAGE PI
syn keyword bbcFunction POINT POS PTR RAD RIGHT$ RND SGN SIN SPC SQR STR$ STRING$ SUM TAB
syn keyword bbcFunction TAN TIME TIME$ USR VAL VPOS AND
syn keyword bbcBoolean TRUE FALSE
syn match bbcProc "\<\(PROC\|FN\)[A-Za-z_][A-Za-z0-9_@`]*"
syn match bbcLineNr "^\s*\d\+"
syn match bbcNumber "\<\d\+\(\.\d*\)\?\([eE][-+]\?\d\+\)\?\>"
syn match bbcNumber "&\x\+\>"
syn match bbcNumber "%[01]\+\>"
syn region bbcString start=+"+ skip=+""+ end=+"+ oneline
syn match bbcComment "\<REM\>.*$"
syn match bbcStar "^\s*\*.*$"
syn match bbcStar ":\s*\*.*$"

hi def link bbcStatement Statement
hi def link bbcFunction  Function
hi def link bbcBoolean   Boolean
hi def link bbcProc      Identifier
hi def link bbcLineNr    LineNr
hi def link bbcNumber    Number
hi def link bbcString    String
hi def link bbcComment   Comment
hi def link bbcStar      PreProc
let b:current_syntax = "bbcbasic"
