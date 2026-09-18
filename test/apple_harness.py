#!/usr/bin/env python3
"""apple_harness.py -- the Apple IIe co-processor on its own, without the
emulator: a stand-in for the shared segment, keys typed down it, the frame
read back.  test/appletest.sh runs it; it is also how to look at what the
Apple shows without the K4510 in the way.

    test/apple_harness.py OUT "ARGS" SECONDS [line ...]

Writes OUT.ppm (the frame, palette applied), OUT.log (the co-processor's
stderr) and prints one line of what it saw.  NOREL=1 in the environment types
without key releases, which is what the emulator's key queue cannot send and
LinApple's keyboard will not take (the emulator taps: down, then up).
"""
import mmap, os, struct, subprocess, sys, time
R=os.path.join(os.path.dirname(os.path.abspath(__file__)),"..","tube","apple")
HDR=32; PAL=768; FB=560*384; OPL=8+4096; PCM=8+8192; KEY=8+1024
SIZE=HDR+PAL+FB+OPL+PCM+KEY
path="/dev/shm/k4510-appletest-%d"%os.getpid()
open(path,"wb").write(b"\0"*SIZE); fd=os.open(path,os.O_RDWR); m=mmap.mmap(fd,SIZE)
m[0:4]=struct.pack("<I",0x4E344D44)
env=dict(os.environ,K4510_DOOM_SHM=path,HOME=os.environ["CLAUDE_JOB_DIR"]+"/tmp")
args=sys.argv[2].split(); p=subprocess.Popen([R+"/apple_k4510"]+args,env=env,stdout=open(sys.argv[1]+".log","wb"),stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL)
def key(code,flags=4,kind=0,val=0):
    w=struct.unpack_from("<I",m,HDR+PAL+FB+OPL+PCM)[0]; struct.pack_into("<I",m,HDR+PAL+FB+OPL+PCM+8+4*(w%256),(val<<24)|(kind<<16)|(flags<<8)|code); struct.pack_into("<I",m,HDR+PAL+FB+OPL+PCM,w+1)
def typeln(s):
    rel = "NOREL" not in os.environ
    for ch in s: key(ord(ch)); time.sleep(0.03); rel and key(0,0,6); time.sleep(0.03)
    key(13); time.sleep(0.03); rel and key(0,0,6)
time.sleep(float(sys.argv[3]) if len(sys.argv)>3 else 6)
for line in (sys.argv[4:] if len(sys.argv)>4 else []): typeln(line); time.sleep(1.5)
w,h=struct.unpack_from("<II",m,20); seq=struct.unpack_from("<I",m,4)[0]; pcm=struct.unpack_from("<II",m,HDR+PAL+FB+OPL)
pal=m[HDR:HDR+PAL]; fb=m[HDR+PAL:HDR+PAL+w*h]
print("frame %dx%d seq %d palette %d colours pcm w=%d"%(w,h,seq,len(set(fb)),pcm[0]))
with open(sys.argv[1]+".ppm","wb") as f:
    f.write(b"P6\n%d %d\n255\n"%(w,h)); f.write(bytes(b for i in fb for b in pal[i*3:i*3+3]))
m[12:16]=struct.pack("<I",1); time.sleep(0.5); p.kill(); os.unlink(path)
