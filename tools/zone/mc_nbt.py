import struct, zlib, gzip, io
def _r(f, t):
    if t==1: return struct.unpack('>b',f.read(1))[0]
    if t==2: return struct.unpack('>h',f.read(2))[0]
    if t==3: return struct.unpack('>i',f.read(4))[0]
    if t==4: return struct.unpack('>q',f.read(8))[0]
    if t==5: return struct.unpack('>f',f.read(4))[0]
    if t==6: return struct.unpack('>d',f.read(8))[0]
    if t==7: n=struct.unpack('>i',f.read(4))[0]; return f.read(n)
    if t==8: n=struct.unpack('>H',f.read(2))[0]; return f.read(n).decode('utf-8','replace')
    if t==9:
        et=f.read(1)[0]; n=struct.unpack('>i',f.read(4))[0]; return [_r(f,et) for _ in range(n)]
    if t==10:
        d={}
        while True:
            tt=f.read(1)[0]
            if tt==0: return d
            nl=struct.unpack('>H',f.read(2))[0]; name=f.read(nl).decode('utf-8','replace'); d[name]=_r(f,tt)
    if t==11: n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>%di'%n,f.read(4*n)))
    if t==12: n=struct.unpack('>i',f.read(4))[0]; return list(struct.unpack('>%dq'%n,f.read(8*n)))
    raise ValueError(t)
def parse(b):
    f=io.BytesIO(b); t=f.read(1)[0]; nl=struct.unpack('>H',f.read(2))[0]; f.read(nl); return _r(f,t)
def load_gz(path): return parse(gzip.open(path).read())
def region_chunks(data):
    """yield (cx,cz local, nbt) from .mca bytes"""
    for i in range(1024):
        off=int.from_bytes(data[i*4:i*4+3],'big'); cnt=data[i*4+3]
        if off==0: continue
        p=off*4096; ln=struct.unpack('>i',data[p:p+4])[0]; comp=data[p+4]
        raw=data[p+5:p+4+ln]
        b=zlib.decompress(raw) if comp==2 else gzip.decompress(raw)
        yield i%32, i//32, parse(b)
