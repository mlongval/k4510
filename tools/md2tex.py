# tools/md2tex.py -- docs/KBASIC.md into the handbook chapter (2026-10-08):
#   python3 tools/md2tex.py docs/KBASIC.md | sed "s/{cha:basic}/{cha:kbasic}/" > doc/guide/chapters/05-basic.tex
# Headings, lists, tables (booktabs), ```code``` as type, `code` as \verb.
import re, sys
src = open(sys.argv[1]).read().splitlines()
def esc(t):
    out = []
    for c in t:
        out.append({'\\': r'\textbackslash{}', '#': r'\#', '$': r'\$', '%': r'\%', '&': r'\&', '_': r'\_',
                    '{': r'\{', '}': r'\}', '~': r'\textasciitilde{}', '^': r'\textasciicircum{}'}.get(c, c))
    return ''.join(out)
def code(t):
    if ' ' in t: return r'\texttt{' + esc(t).replace('-', '-{}') + '}'   # breakable at its spaces
    for d in '|!+@':
        if d not in t: return r'\verb' + d + t + d
    return r'\texttt{' + esc(t) + '}'
def inline(t):
    parts = re.split(r'(`[^`]*`)', t); r = []
    for p in parts:
        if p.startswith('`') and p.endswith('`') and len(p) > 1: r.append(code(p[1:-1])); continue
        p = esc(p)
        p = re.sub(r'\*\*(.+?)\*\*', r'\\textbf{\1}', p)
        p = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'\\emph{\1}', p)
        p = re.sub(r'(^|[\s(])"', r'\1``', p); p = p.replace('"', "''")
        p = p.replace(' -- ', ' --- ')
        r.append(p)
    return ''.join(r)
out = []; i = 0; inlist = False
def endlist():
    global inlist
    if inlist: out.append(r'\end{itemize}'); inlist = False
while i < len(src):
    l = src[i]
    if l.startswith('```'):
        endlist(); i += 1; out.append(r'\begin{type}')
        while not src[i].startswith('```'): out.append(src[i]); i += 1
        out.append(r'\end{type}'); i += 1; continue
    if l.startswith('|'):
        endlist(); rows = []
        while i < len(src) and src[i].startswith('|'):
            if not re.match(r'^\|[-| :]+\|$', src[i]):
                rows.append([c.strip() for c in src[i].strip('|').split(' | ')])
            i += 1
        n = len(rows[0]); P = r'>{\raggedright\arraybackslash}p{%.2f\linewidth}'
        if n == 2 and max(len(r[0]) for r in rows) > 26: spec = (P % 0.46) + ' ' + (P % 0.46)
        else:
            w = [max(len(r[k]) for r in rows) for k in range(n)]
            short = [x <= 14 for x in w]; left = 0.88 - 0.16 * sum(short); longs = n - sum(short) or 1
            spec = ' '.join('l' if short[k] else P % (left / longs) for k in range(n))
        out += [r'\begin{center}', r'\small', r'\begin{tabular}{@{}' + spec + '@{}}', r'\toprule']
        out.append(' & '.join(r'\textbf{' + inline(c) + '}' for c in rows[0]) + r' \\'); out.append(r'\midrule')
        for r in rows[1:]: out.append(' & '.join(inline(c) for c in r) + r' \\')
        out += [r'\bottomrule', r'\end{tabular}', r'\end{center}']; continue
    m = re.match(r'^(#+) (.*)', l)
    if m:
        endlist(); lvl = len(m.group(1)); t = inline(m.group(2))
        if lvl == 1: out.append(r'\chapter{' + t + r'}\label{cha:basic}')
        else: out.append(('\\section{' if lvl == 2 else '\\subsection{') + t + '}')
        i += 1; continue
    m = re.match(r'^- (.*)', l)
    if m:
        if not inlist: out.append(r'\begin{itemize}'); inlist = True
        item = m.group(1); i += 1
        while i < len(src) and src[i].startswith('  ') and src[i].strip(): item += ' ' + src[i].strip(); i += 1
        out.append(r'\item ' + inline(item)); continue
    if not l.strip(): endlist(); out.append(''); i += 1; continue
    endlist(); out.append(inline(l)); i += 1
endlist()
print('\n'.join(out))
