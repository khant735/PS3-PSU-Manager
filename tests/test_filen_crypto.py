"""Independent Python crypto fixtures exercise the native C++ implementation."""
import base64, hashlib, pathlib, subprocess, tempfile
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/'tests/filen_crypto_test'
key=b'0123456789abcdefghijklmnopqrstuv'
plain=b'Native PS3 Filen test\x00\xff\x80' * 100
iv=b'abcdefghijkl'
count=0
with tempfile.TemporaryDirectory() as d:
 def check(data,version,k,meta=False,bad=False,expected=plain):
  global count
  p=pathlib.Path(d)/'input';p.write_bytes(data)
  r=subprocess.run([str(exe),str(version),str(p),'meta' if meta else 'chunk',k],capture_output=True)
  assert (r.returncode!=0) if bad else (r.returncode==0 and r.stdout==expected),r.stderr
  count+=1
 for v in (2,3):
  k=key.decode() if v==2 else key.hex()
  data=iv+AESGCM(key).encrypt(iv,plain,None)
  check(data,v,k)
  check(data[:-1]+bytes([data[-1]^1]),v,k,bad=True)
  check(data[:20],v,k,bad=True)
  check(data,v,'x'*32,bad=True)
  mkey=hashlib.pbkdf2_hmac('sha512',key,key,1,32) if v==2 else key
  meta=(b'002'+iv if v==2 else b'003'+iv.hex().encode())+base64.b64encode(AESGCM(mkey).encrypt(iv,b'Save.psu',None))
  check(meta,v,k,True,expected=b'Save.psu')
  check(meta,v,'x'*len(k),True,bad=True)
 # Legacy CryptoJS/OpenSSL salted AES-CBC.
 salt=b'12345678';derived=b'';prev=b''
 while len(derived)<48:
  prev=hashlib.md5(prev+key+salt).digest();derived+=prev
 pad=16-len(plain)%16
 enc=Cipher(algorithms.AES(derived[:32]),modes.CBC(derived[32:48])).encryptor()
 data=b'Salted__'+salt+enc.update(plain+bytes([pad])*pad)+enc.finalize()
 check(data,1,key.decode())
 check(base64.b64encode(data),1,key.decode())
 check(base64.b64encode(data),1,key.decode(),True)
 check(data,99,key.decode(),bad=True)
print(f'{count} native crypto cases passed')
