#!/usr/bin/env python3
"""jstcat.py FILE.jst [--phases] [--dump PHASE]: what a recorded stream holds."""
import sys, struct, re, collections, gzip
def read(path):
    d = (gzip.open if path.endswith('.gz') else open)(path, 'rb').read()
    nl = d.index(b'\n'); hdr = d[:nl].split(); i = nl + 1; recs = []
    while i < len(d):
        tag = d[i:i+1]; t, n = struct.unpack('<II', d[i+1:i+9]); recs.append((tag, t, d[i+9:i+9+n])); i += 9 + n
    return int(hdr[1]), int(hdr[2]), recs
if __name__ == '__main__':
    cols, rows, recs = read(sys.argv[1])
    ph = 'start'; st = collections.OrderedDict()
    seqs = collections.Counter()
    for tag, t, b in recs:
        if tag == b'M': ph = b.decode(); continue
        s = st.setdefault(ph, [0, 0, 0])
        if tag == b'O':
            s[0] += len(b); s[1] += 1
            for m in re.finditer(rb'\x1b(\[[?>=<]?[0-9;:]*[ -/]*[@-~]|\][^\x07\x1b]*|[^\[\]])', b):
                k = re.sub(rb'[0-9]+', b'n', m.group(0)); seqs[k] += 1
        else: s[2] += 1
    print(f'{cols}x{rows}')
    for k, v in st.items(): print(f'  {k:10s} {v[0]:8d} bytes {v[1]:5d} chunks {v[2]:4d} keys')
    print('sequences:')
    for k, v in seqs.most_common(60): print(f'  {v:7d}  {k!r}')
    if '--dump' in sys.argv:
        want = sys.argv[sys.argv.index('--dump') + 1]; ph = 'start'
        for tag, t, b in recs:
            if tag == b'M': ph = b.decode(); continue
            if ph == want: print(tag, t, repr(b)[:600])
