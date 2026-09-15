-- tools/nvim/init.lua -- Neovim as the K4510's (NVIM; tools/k4510-nvim).
--
-- Doc, 2026-09-15: "make the neovim tube and the proper setup for our target
-- languages and the make sequence".  The machine's own editor is VI; this is
-- the Linux's, dressed for the machine:
--   * the colours are JIM's, which are the machine's palette, and nothing is
--     drawn that CP437 has not got;
--   * its languages by their extensions, in capitals as the machine writes
--     them (HELLO.C is C, not the C++ Vim takes *.C for);
--   * :make (F9) compiles with the machine's compilers, k4510-cc and
--     k4510-pas, and reads /SYSTEM/LOG/MAKE.ERR into the error list -- the
--     file VI's :make reads, so the two can never disagree -- and puts you on
--     the first error;  :cn :cp :cl as in VI;
--   * :Run (F10) builds and the machine runs it: the command goes to NVIM
--     (fs/SYSTEM/BIN/nvim.prg) in /SYSTEM/LOG/NVIM.BAT, and Neovim comes back
--     where it was when the program is done.
local uv = vim.uv or vim.loop
local here = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h")
vim.opt.runtimepath:prepend(here)

local o = vim.opt
o.termguicolors = false          -- JIM's sixteen, which are the palette's
o.number = true
o.ruler = true
o.laststatus = 2
o.showmode = true
o.ttimeoutlen = 10               -- Esc at once: JIM sends each key as it comes
o.mouse = ""                     -- the machine's mouse is the machine's
o.expandtab = true
o.shiftwidth = 4
o.softtabstop = 4
o.ignorecase = true
o.smartcase = true
o.fileformats = "unix,dos"
o.shortmess:append("I")
-- only characters CP437 has: JIM draws the rest as a likeness
o.fillchars = { vert = "│", fold = "-", eob = "~" }
o.listchars = { tab = "» ", trail = "·", nbsp = "+" }
vim.cmd("syntax on")
vim.cmd("filetype plugin indent on")
vim.cmd("colorscheme k4510")

-- ---- the machine's languages ------------------------------------------------
local ext = { C = "c", H = "c", PAS = "pascal", RX = "rexx", BAS = "k4510basic",
              BBC = "bbcbasic", LGO = "k4510logo", K4P = "dosini" }
local both = {}
for k, v in pairs(ext) do both[k] = v; both[k:lower()] = v end
vim.filetype.add({ extension = both })

-- what builds each, and what runs each (VI's compiler() and interpreted())
local langs = {
    c = { tool = "k4510-cc" },   pascal = { tool = "k4510-pas" },
    rexx = { run = "RX" },       k4510basic = { run = "MSBASIC" },   k4510logo = { run = "LOGO" },
}

-- ---- where things are -----------------------------------------------------------
local root = os.getenv("K4510_ROOT")
if root then root = uv.fs_realpath(root) or root end
local log = (root or ".") .. "/SYSTEM/LOG"
local function note(s, hl) vim.api.nvim_echo({ { s, hl or "None" } }, false, {}) end
local function guest(path)                 -- a host path as the machine names it: /HOME/SIEVE.C
    if not root then return nil end
    local p = uv.fs_realpath(path) or path
    if p:sub(1, #root + 1) ~= root .. "/" then return nil end
    return p:sub(#root + 1)
end
local function project(dir)                -- a PROJECT.K4P beside the file, whatever its case (PROG's projects)
    for _, f in ipairs(vim.fn.readdir(dir)) do if f:upper() == "PROJECT.K4P" then return f end end
end
local function proj_prog(dir, f)           -- the program it makes: OUT= if given, else NAME=
    local name
    for l in io.lines(dir .. "/" .. f) do
        local k, v = l:match("^%s*(%u+)%s*=%s*(%S+)")
        if k == "OUT" then return (v:gsub("%.[pP][rR][gG]$", "")) end
        if k == "NAME" then name = v end
    end
    return name
end
-- /SYSTEM/LOG/MAKE.ERR: FILE:LINE:COL:KIND:TEXT, one message a line (tools/k4510-errfmt);
-- FILE is the source's name beside it, a machine path, or - for none
local function read_errors(dir)
    local items, nerr = {}, 0
    local f = io.open(log .. "/MAKE.ERR")
    if not f then return items, 0 end
    for l in f:lines() do
        local file, ln, col, kind, text = l:match("^([^:]*):(%d+):(%d+):(%a):(.*)$")
        if file then
            local it = { lnum = tonumber(ln), col = tonumber(col), type = kind, text = text }
            if file ~= "-" then
                if file:sub(1, 1) == "/" then
                    it.filename = (root and not uv.fs_stat(file)) and (root .. file) or file
                else
                    it.filename = dir .. "/" .. file
                end
            end
            if kind == "E" then nerr = nerr + 1 end
            items[#items + 1] = it
        end
    end
    f:close()
    return items, nerr
end
local function show_first(items)           -- to the first error (or the first message), and say it
    local first = 1
    for i, it in ipairs(items) do if it.type == "E" then first = i; break end end
    pcall(vim.cmd, "cc " .. first)
    local it = items[first]
    note(("%s %d of %d: %s"):format(it.type == "W" and "warning" or "error", first, #items, it.text), "ErrorMsg")
end

-- ---- :make ------------------------------------------------------------------------
local function make()
    local file = vim.api.nvim_buf_get_name(0)
    if file == "" then note("make: the file has no name -- :w NAME first", "ErrorMsg"); return false end
    local L = langs[vim.bo.filetype]
    if not L then note("make: no compiler for this file (.C .PAS .RX .BAS .LGO)", "WarningMsg"); return false end
    if vim.bo.modified then vim.cmd("write") end
    if not L.tool then note("saved -- nothing to compile: run it (:Run, F10)"); return true end
    local dir = vim.fn.fnamemodify(file, ":p:h")
    local p = project(dir)
    local args = p and ("-p " .. vim.fn.shellescape(p)) or vim.fn.shellescape(vim.fn.fnamemodify(file, ":t"))
    note("compiling ...")
    local out = vim.fn.system("cd " .. vim.fn.shellescape(dir) .. " && " .. L.tool .. " " .. args .. " 2>&1")
    local rc = vim.v.shell_error
    local items, nerr = read_errors(dir)
    vim.fn.setqflist({}, "r", { title = "make: " .. vim.fn.fnamemodify(file, ":t"), items = items })
    if rc == 0 and nerr == 0 then
        local nw = #items
        note(nw > 0 and ("compiled, " .. nw .. (nw == 1 and " warning (:cn)" or " warnings (:cn)")) or "compiled")
        return true
    end
    if #items > 0 then show_first(items)
    else
        local last = out:gsub("%s+$", ""):match("[^\n]*$")
        note(("make: failed, rc %d -- %s"):format(rc, (last and last ~= "") and last or "nothing in MAKE.ERR"), "ErrorMsg")
    end
    return false
end

-- ---- :Run -------------------------------------------------------------------------
local function run()
    local file = vim.api.nvim_buf_get_name(0)
    local L = langs[vim.bo.filetype]
    if not make() then return end
    local g = guest(file)
    if not g then note("run: the file is not on the machine's disk", "ErrorMsg"); return end
    local cmd
    if L.run then cmd = L.run .. " " .. g                   -- REXX, BASIC, LOGO: the interpreter runs the file
    else
        local dir = vim.fn.fnamemodify(file, ":p:h")
        local p = project(dir)
        local gdir = g:match("^(.*)/[^/]*$") or ""
        cmd = p and (gdir .. "/" .. (proj_prog(dir, p) or "")) or (g:gsub("%.[^./]*$", ""))
    end
    local f = io.open(log .. "/NVIM.RESUME", "w")
    if not f then note("run: cannot write " .. log, "ErrorMsg"); return end
    f:write(file, "\n", vim.fn.line("."), "\n"); f:close()
    f = io.open(log .. "/NVIM.BAT", "w")
    f:write("SWAP -k ", cmd, "\n"); f:close()               -- as VI's :run: over the editor, and the editor back
    vim.cmd("wall")
    vim.cmd("qall")
end

vim.api.nvim_create_user_command("Make", make, { desc = "compile with the machine's compiler" })
vim.api.nvim_create_user_command("Run", run, { desc = "build, and the machine runs it" })
vim.cmd([[cnoreabbrev <expr> make (getcmdtype() ==# ':' && getcmdline() ==# 'make') ? 'Make' : 'make']])
vim.cmd([[cnoreabbrev <expr> run (getcmdtype() ==# ':' && getcmdline() ==# 'run') ? 'Run' : 'run']])
vim.keymap.set("n", "<F9>", "<Cmd>Make<CR>")
vim.keymap.set("i", "<F9>", "<Esc><Cmd>Make<CR>")
vim.keymap.set("n", "<F10>", "<Cmd>Run<CR>")
vim.keymap.set("i", "<F10>", "<Esc><Cmd>Run<CR>")

-- back from a run: say so, and to the line a REXX program stopped at (VI's :run does the same)
if os.getenv("K4510_NVIM_RAN") then
    vim.api.nvim_create_autocmd("VimEnter", { once = true, callback = function()
        if vim.bo.filetype == "rexx" then
            local items = read_errors(vim.fn.expand("%:p:h"))
            if #items > 0 then vim.fn.setqflist({}, "r", { title = "run", items = items }); show_first(items); return end
        end
        note("ran it; the file is as saved")
    end })
end
