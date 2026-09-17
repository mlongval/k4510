#!/usr/bin/env python3
"""Install the K4510 target into a Mad-Pascal checkout and rebuild mp.

    pascal/install.py [MP_DIR]      (default: ~/Projects/K4510/toolchain/Mad-Pascal)

Copies pascal/mp/base and pascal/mp/lib over the checkout (new files only:
base/rtl6502_k4510.asm, base/k4510/, lib/*_k4510.inc, lib/k4510.pas),
patches src/Targets.pas and src/mp.pas to know the target (idempotent),
and runs make in src/. Needs fpc. Upstream is untouched otherwise; the
patch is small enough to send upstream one day.
"""
import os, sys, shutil, subprocess, re
here = os.path.dirname(os.path.abspath(__file__))
mp = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Projects/K4510/toolchain/Mad-Pascal'))
if not os.path.isfile(os.path.join(mp, 'src', 'Targets.pas')):
    sys.exit(f'{mp}: not a Mad-Pascal checkout (no src/Targets.pas)')
# 1. files
for sub in ('base', 'lib'):
    src = os.path.join(here, 'mp', sub)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        dst = os.path.join(mp, sub, rel)
        os.makedirs(dst, exist_ok=True)
        for f in files:
            shutil.copy2(os.path.join(root, f), os.path.join(dst, f))
            print('  ', os.path.join(sub, rel, f))
# 1b. lib/targets/*.inc dispatch on the target's define; graph and sysutils get raw's stubs until there are real ones
for unit in ('crt', 'crth', 'system', 'systemh', 'sysutils', 'graph', 'graphh'):
    disp = os.path.join(mp, 'lib', 'targets', unit + '.inc')
    if not os.path.isfile(disp): continue
    d = open(disp).read()
    if 'k4510' not in d:
        d = d.rstrip('\n') + '\n{$ifdef k4510}\n  {$i ' + unit + '_k4510.inc}\n{$endif}\n'
        open(disp, 'w').write(d); print('   patched lib/targets/' + unit + '.inc')
    mine = os.path.join(mp, 'lib', unit + '_k4510.inc'); raw = os.path.join(mp, 'lib', unit + '_raw.inc')
    if not os.path.isfile(mine) and os.path.isfile(raw):
        shutil.copy2(raw, mine); print('   lib/' + unit + '_k4510.inc (raw stub)')
# 2. Targets.pas
p = os.path.join(mp, 'src', 'Targets.pas'); s = open(p).read()
block = '''    TTargetID.K4510: begin
      // K4510 target [K4510]: a .prg (4-byte header: load address, run address)
      // for the system ROM's RUN; the 45GS02 runs the 65C02 set; JIM ($DA00) is the console.
      // Zero page: $22-$3F the registers, $64-$A3 the software stack (-stack); the ROM owns $02-$21 and $F0-$F9.
      target.cpu := TCPU.cpu_65c02;
      target.Name := 'K4510';
      target.buf := $0300;
      target.zpage := $0022;
      target.eol := $0000000D;
      target.codeorigin := $0800;
      target.header[0] := 'opt h-f+';
      target.header[1] := 'org $07FC';
      target.header[2] := 'dta a($0800),a(START)';
      target.header[3] := 'org $0800';
      target.header[4] := 'END';
    end;
'''
if 'TTargetID.K4510: begin' in s and block not in s:          # a stale version of the block: out with it
    i = s.index('    TTargetID.K4510: begin'); j = s.index('    end;\n', i) + len('    end;\n')
    s = s[:i] + s[j:]; print('   src/Targets.pas: old K4510 block removed')
if 'TTargetID.K4510: begin' not in s:
    if 'K4510);' not in s: s = s.replace('TTargetID = (NONE, A8, C4P, C64, NEO, RAW, X16);', 'TTargetID = (NONE, A8, C4P, C64, NEO, RAW, X16, K4510);')
    anchor = '    TTargetID.X16: begin'
    assert anchor in s
    s = s.replace(anchor, block + anchor)
    open(p, 'w').write(s); print('   patched src/Targets.pas')
else:
    print('   src/Targets.pas already knows K4510')
# 3. mp.pas: the -target parser and the default stack base for the target
import glob
p = [f for f in glob.glob(os.path.join(mp, 'src', '*.pas')) + glob.glob(os.path.join(mp, 'src', '*', '*.inc')) if 'function ParseTargetParameter' in open(f, errors='replace').read()][0]
s = open(p).read()
if "parameterValue = 'K4510'" not in s:
    s, n = re.subn(r"(\n(\s*)else if parameterValue = 'X16' then Result := TTargetID.X16)",
                   r"\1\n\2else if parameterValue = 'K4510' then Result := TTargetID.K4510", s, count=1)
    assert n == 1, 'ParseTargetParameter not found'
    s = s.replace("NEO, RAW, X16''.');", "NEO, RAW, X16, K4510''.');", 1)
    open(p, 'w').write(s); print('   patched', os.path.relpath(p, mp) + ': -target:k4510')
p = os.path.join(mp, 'src', 'mp.pas'); s = open(p).read()
if 'STACK_BASE := $64' not in s:
    # the software stack in zero page at $64 unless -stack says otherwise
    m = re.search(r'\n(\s*)if ZPAGE_BASE < 0 then\n', s)
    assert m, 'ZPAGE_BASE default not found'
    s = s.replace(m.group(0), '\n' + m.group(1) + 'if (STACK_BASE < 0) and (targetID = TTargetID.K4510) then STACK_BASE := $64;   // [K4510]\n' + m.group(0), 1)
    open(p, 'w').write(s); print('   patched src/mp.pas: stack at $64')
# 3b. lib/system.pas: the SINGLE transcendentals on the MATH unit, the originals kept under {$else}
p = os.path.join(mp, 'lib', 'system.pas'); s = open(p).read()
def mathfn(header, op):
    name = header.split('(')[0].split()[-1]; arg = header.split('(')[1].split(':')[0]
    return (header + '\nbegin\n asm\n\tlda ' + arg + '\n\tsta $D700\n\tlda ' + arg + '+1\n\tsta $D701\n\tlda ' + arg + '+2\n\tsta $D702\n\tlda ' + arg + '+3\n\tsta $D703\n'
            '\tstz $D721\n\tlda #' + str(op) + '\n\tsta $D720\t; [K4510] the MATH unit: ' + name + '\n'
            '\tlda $D700\n\tsta Result\n\tlda $D701\n\tsta Result+1\n\tlda $D702\n\tsta Result+2\n\tlda $D703\n\tsta Result+3\n end;\nend;\n')
if '[K4510] the MATH unit' not in s:
    for header, op in (('function Sqrt(x: Single): Single; overload;', 5), ('function Sin(x: single): single; overload;', 6),
                       ('function Cos(x: single): single; overload;', 7), ('function ArcTan(a: single): single; overload;', 9),
                       ('function Exp(x: Float): Float; overload;', 11), ('function Ln(x: Float): Float; overload;', 12)):
        i = s.find('\n' + header + '\n')
        if i < 0: print('   system.pas: no', header); continue
        i += 1; j = s.index('\nend;\n', i) + len('\nend;\n')
        s = s[:i] + '{$ifdef k4510}\n' + mathfn(header, op) + '{$else}\n' + s[i:j] + '{$endif}\n' + s[j:]
    open(p, 'w').write(s); print('   patched lib/system.pas: Sqrt/Sin/Cos/ArcTan/Exp/Ln on the MATH unit')
else:
    print('   lib/system.pas already routes the transcendentals to the MATH unit')
# 4. build
subprocess.run(['make', 'clean'], cwd=os.path.join(mp, 'src'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   # FPC keeps stale units otherwise
r = subprocess.run(['make', '-s'], cwd=os.path.join(mp, 'src'))
if r.returncode: sys.exit('mp build failed')
print('   built', os.path.join(mp, 'bin', 'mp'))
