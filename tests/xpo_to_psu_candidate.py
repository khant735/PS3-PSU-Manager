#!/usr/bin/env python3
"""Experimental X-Port v1 to PSU converter. Never writes to memory cards."""
import argparse,struct,pathlib,hashlib,json
DIR=0x8427; FILE=0x8497

def entry(name,attr,size,ct=b'\0'*8,mt=b'\0'*8):
    h=bytearray(512);struct.pack_into('<H',h,0,attr);struct.pack_into('<I',h,4,size);h[8:16]=ct;h[24:32]=mt
    nb=name.encode('ascii');assert len(nb)<=32;h[64:64+len(nb)]=nb
    return h

def convert(src,dst):
    b=pathlib.Path(src).read_bytes()
    assert len(b)>=552 and b[:6]==bytes.fromhex('010000002a01') and struct.unpack_from('<I',b,8)[0]==len(b)-4
    count=struct.unpack_from('<H',b,6)[0];assert 0<count<=64
    root=b[0x36:0x36+64].split(b'\0',1)[0].decode('ascii');assert root and len(root)<=32
    # Outer header: attributes are stored big endian, timestamp bytes use PS2 8-byte layout.
    rootattr=int.from_bytes(b[0x82:0x84],'big');assert rootattr==DIR
    pos=302; records=[]
    for i in range(count):
        assert b[pos:pos+2]==b'\xfa\x00'
        name=b[pos+2:pos+66].split(b'\0',1)[0].decode('ascii')
        size=struct.unpack_from('<I',b,pos+66)[0]
        assert name and len(name)<=32 and pos+250+size<=len(b)
        attr=int.from_bytes(b[pos+78:pos+80],'big');assert attr==FILE
        records.append((name,b[pos+250:pos+250+size],b[pos+82:pos+90],b[pos+90:pos+98]))
        pos+=250+size
    assert pos==len(b)
    out=bytearray(entry(root,DIR,len(records)+2,b[0x86:0x8e],b[0x8e:0x96]))
    out+=entry('.',DIR,0)+entry('..',DIR,0)
    for name,payload,ct,mt in records:
        out+=entry(name,FILE,len(payload),ct,mt)
        out+=payload
        out+=b'\0'*((-len(payload))%1024)
    pathlib.Path(dst).write_bytes(out)
    return dict(source=str(src),output=str(dst),root=root,files=[{'name':n,'bytes':len(p),'sha256':hashlib.sha256(p).hexdigest()} for n,p,_,_ in records],output_size=len(out))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('source');ap.add_argument('output');a=ap.parse_args()
    print(json.dumps(convert(a.source,a.output),indent=2))
