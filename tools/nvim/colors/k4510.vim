" tools/nvim/colors/k4510.vim -- Neovim in the machine's colours.
" JIM turns the terminal's sixteen colours into the machine's palette, and its
" own colours -- yellow on blue unless PALETTE or COLOR said otherwise -- are
" Normal's, so nothing here names an RGB.  A palette the person loads recolours
" Neovim with the rest of the machine.  No blue on blue: colour 4 and 12 are
" used only behind text.
hi clear
if exists("syntax_on") | syntax reset | endif
let g:colors_name = "k4510"

hi Normal       ctermfg=NONE ctermbg=NONE
hi Comment      ctermfg=6
hi Constant     ctermfg=10
hi String       ctermfg=10
hi Character    ctermfg=10
hi Number       ctermfg=13
hi Boolean      ctermfg=13
hi Float        ctermfg=13
hi Identifier   ctermfg=NONE
hi Function     ctermfg=14
hi Statement    ctermfg=15 cterm=bold
hi Keyword      ctermfg=15 cterm=bold
hi Operator     ctermfg=NONE
hi PreProc      ctermfg=13
hi Type         ctermfg=14
hi Special      ctermfg=9
hi Underlined   cterm=underline
hi Error        ctermfg=15 ctermbg=1
hi Todo         ctermfg=0 ctermbg=11

hi LineNr       ctermfg=6
hi CursorLineNr ctermfg=11 cterm=bold
hi StatusLine   ctermfg=0 ctermbg=7 cterm=NONE
hi StatusLineNC ctermfg=0 ctermbg=6 cterm=NONE
hi VertSplit    ctermfg=7 ctermbg=NONE cterm=NONE
hi WinSeparator ctermfg=7 ctermbg=NONE cterm=NONE
hi Visual       ctermfg=0 ctermbg=7
hi Search       ctermfg=0 ctermbg=11
hi IncSearch    ctermfg=0 ctermbg=14 cterm=NONE
hi CurSearch    ctermfg=0 ctermbg=14
hi MatchParen   ctermfg=0 ctermbg=14
hi Pmenu        ctermfg=0 ctermbg=7
hi PmenuSel     ctermfg=15 ctermbg=0
hi NonText      ctermfg=6
hi EndOfBuffer  ctermfg=6
hi SpecialKey   ctermfg=6
hi Folded       ctermfg=0 ctermbg=6
hi ErrorMsg     ctermfg=15 ctermbg=1
hi WarningMsg   ctermfg=11 cterm=bold
hi ModeMsg      ctermfg=15 cterm=bold
hi MoreMsg      ctermfg=10
hi Question     ctermfg=10
hi Title        ctermfg=15 cterm=bold
hi Directory    ctermfg=14
hi QuickFixLine ctermfg=0 ctermbg=11
hi qfFileName   ctermfg=14
hi qfLineNr     ctermfg=11
hi DiffAdd      ctermfg=0 ctermbg=10
hi DiffChange   ctermfg=0 ctermbg=14
hi DiffDelete   ctermfg=15 ctermbg=1
hi DiffText     ctermfg=0 ctermbg=11
hi FloatBorder  ctermfg=7
hi NormalFloat  ctermfg=15 ctermbg=0
