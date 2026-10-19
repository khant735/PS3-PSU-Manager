#!/usr/bin/env python3
"""Verify experimental XPO->PSU reconstruction against original record bytes."""
import sys,struct,hashlib,pathlib
from xpo_to_psu_candidate import convert

def check(src,dst):
 b=pathlib.Path(src).read_bytes(); p=pathlib.Path(dst).read_bytes()
 count=struct.unpack_from('<H',b,6)[0]; pos=302; expected=[]
 for i in range(count):
  assert b[pos:pos+2]==b'\xfa\0'
  name=b[pos+2:pos+66].split(b'\0',1)[0]
  size=struct.unpack_from('<I',b,pos+66)[0]
  expected.append((name,b[pos+250:pos+250+size]));pos+=250+size
 assert pos==len(b)
 root=b[0x36:0x76].split(b'\0',1)[0]
 assert p[64:64+len(root)]==root
 assert struct.unpack_from('<I',p,4)[0]==count+2
 off=1536
 for name,payload in expected:
  hdr=p[off:off+512]; assert len(hdr)==512
  assert hdr[64:64+len(name)]==name
  assert struct.unpack_from('<I',hdr,4)[0]==len(payload)
  assert p[off+512:off+512+len(payload)]==payload
  off+=512+((len(payload)+1023)//1024)*1024
 assert off==len(p)
 return [(n.decode('ascii'),len(v),hashlib.sha256(v).hexdigest()) for n,v in expected]

if __name__=='__main__':
 for arg in sys.argv[1:]:
  src=pathlib.Path(arg); dst=src.with_suffix('.experimental.psu')
  convert(src,dst)
  print(src.name, 'PASS',check(src,dst))
 print('WARNING: structural and byte-integrity checks only; game compatibility NOT established.')
