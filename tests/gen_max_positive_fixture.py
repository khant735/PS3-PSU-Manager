import struct
from pathlib import Path
NCHAR=314; Q1=32768;Q2=65536;Q3=98304;Q4=131072

def encode(data):
    cum=list(range(NCHAR, -1,-1)); freq=[0]+[1]*NCHAR
    c2s=list(range(1,NCHAR+1)); s2c=[0]+list(range(NCHAR));low=0;high=Q4;shifts=0;bits=[]
    def emit(bit):
        nonlocal shifts
        bits.append(bit);bits.extend([1-bit]*shifts);shifts=0
    for ch in data:
        sym=c2s[ch];rng=high-low
        high=low+rng*cum[sym-1]//cum[0];low+=rng*cum[sym]//cum[0]
        while True:
            if high<=Q2:emit(0)
            elif low>=Q2:emit(1);low-=Q2;high-=Q2
            elif low>=Q1 and high<=Q3:shifts+=1;low-=Q1;high-=Q1
            else:break
            low*=2;high*=2
        if cum[0]>=Q1-1:
            c=0
            for i in range(NCHAR,0,-1):
                cum[i]=c;freq[i]=(freq[i]+1)//2;c+=freq[i]
            cum[0]=c
        i=sym
        while i>1 and freq[i]==freq[i-1]:i-=1
        if i<sym:
            a=s2c[i];b=s2c[sym];s2c[i]=b;s2c[sym]=a;c2s[a]=sym;c2s[b]=i
        freq[i]+=1
        for j in range(i):cum[j]+=1
    shifts+=1;emit(0 if low<Q1 else 1)
    bits.extend([0]*32)
    bits.extend([0]*((-len(bits))%8))
    return struct.pack('<I',len(data))+bytes(sum(bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(bits),8))

name=b'icon.sys\0';payload=b'VALID-MAX-FILE-DATA';entry=struct.pack('<I',len(payload))+name.ljust(32,b'\0')+payload
entry+=b'\0'*((-(len(entry)+8))%16)
compressed=encode(entry)
hdr=b'Ps2PowerSave'+struct.pack('<I',0)+b'BASLUS-12345\0'.ljust(32,b'\0')+b'icon.sys\0'.ljust(32,b'\0')+struct.pack('<II',len(compressed),1)
archive=hdr+compressed
(Path(__file__).parent / 'fixtures' / 'max_synthetic_positive.max').write_bytes(archive)
print(len(entry),len(compressed),len(archive))
