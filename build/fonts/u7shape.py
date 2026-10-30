# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 the exult-3ds authors
import struct
def flex_entries(d):
    n=struct.unpack_from('<I',d,0x54)[0]
    return [struct.unpack_from('<II',d,0x80+8*i) for i in range(n)]
def decode_frame(b):
    xr,xl,ya,yb=struct.unpack_from('<hhhh',b,0)
    w=xl+xr+1; h=ya+yb+1
    img=[[None]*w for _ in range(h)]
    p=8
    while True:
        sl=struct.unpack_from('<H',b,p)[0]; p+=2
        if sl==0: break
        enc=sl&1; ln=sl>>1
        sx,sy=struct.unpack_from('<hh',b,p); p+=4
        x=sx+xl; y=sy+ya
        if not enc:
            for i in range(ln): img[y][x+i]=b[p+i]
            p+=ln
        else:
            bi=0
            while bi<ln:
                c=b[p]; p+=1; rep=c&1; c>>=1
                if rep:
                    v=b[p]; p+=1
                    for i in range(c): img[y][x+bi+i]=v
                else:
                    for i in range(c): img[y][x+bi+i]=b[p+i]
                    p+=c
                bi+=c
    return dict(xr=xr,xl=xl,ya=ya,yb=yb,img=img)
def decode_shape(s):
    dlen,hdr=struct.unpack_from('<II',s,0)
    n=(hdr-4)//4
    offs=[hdr]+[struct.unpack_from('<I',s,8+4*(i-1))[0] for i in range(1,n)]
    frames=[]
    for i,o in enumerate(offs):
        end=offs[i+1] if i+1<n else dlen
        frames.append(decode_frame(s[o:end]) if end>o else None)
    return frames
def encode_frame(img, xl, ya):
    # img: rows of None/palette index
    h=len(img); w=len(img[0]) if h else 0
    out=bytearray(struct.pack('<hhhh', w-xl-1, xl, ya, h-ya-1))
    for y in range(h):
        x=0
        while x<w:
            if img[y][x] is None: x+=1; continue
            x0=x
            while x<w and img[y][x] is not None: x+=1
            run=img[y][x0:x]
            # raw scan (not encoded): simplest; split into chunks <= 32767
            out+=struct.pack('<Hhh',(len(run)<<1)|0, x0-xl, y-ya)
            out+=bytes(run)
    out+=struct.pack('<H',0)
    return bytes(out)
def encode_shape(frames):
    n=len(frames)
    hdr=4+4*n
    body=b''; offs=[]
    for f in frames:
        offs.append(hdr+len(body)); body+=f
    dlen=hdr+len(body)
    return struct.pack('<I',dlen)+struct.pack('<I',offs[0])+b''.join(struct.pack('<I',o) for o in offs[1:])+body
def build_flex(entries, title=b'Exult modern fonts'):
    n=len(entries)
    hdr=bytearray(0x80)
    hdr[:len(title)]=title
    struct.pack_into('<IIII',hdr,0x50,0xFFFF1A00,n,1,0x80+8*n+0)  # magic,count,version,eof? (fixed below)
    table=bytearray(); data=bytearray(); off=0x80+8*n
    for e in entries:
        if e: table+=struct.pack('<II',off+len(data),len(e)); data+=e
        else: table+=struct.pack('<II',0,0)
    out=bytes(hdr)+bytes(table)+bytes(data)
    out=bytearray(out); struct.pack_into('<I',out,0x5C,len(out))
    return bytes(out)
