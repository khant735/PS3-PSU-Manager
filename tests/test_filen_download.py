import base64, hashlib, json, pathlib, subprocess, tempfile
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
root=pathlib.Path(__file__).resolve().parents[1]
key=b'0123456789abcdefghijklmnopqrstuv';iv=b'abcdefghijkl'
meta_key=hashlib.pbkdf2_hmac('sha512',key,key,1,32)
def meta(s):return '002'+iv.decode()+base64.b64encode(AESGCM(meta_key).encrypt(iv,s.encode(),None)).decode()
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp);dest=p/'downloads';dest.mkdir()
 plain=bytes(range(256))*5000;(p/'plain').write_bytes(plain)
 for i,start in enumerate(range(0,len(plain),1048576)):
  (p/str(i)).write_bytes(iv+AESGCM(key).encrypt(iv,plain[start:start+1048576],None))
 (p/'info.json').write_text(json.dumps({'status':True,'data':{'name':meta('Test.psu'),'size':meta(str(len(plain))),'uuid':'22222222-2222-2222-2222-222222222222','region':'de-1','bucket':'1','version':2,'chunks':2,'downloadBtn':True}}))
 subprocess.run([str(root/'tests/filen_download_test'),str(p),str(dest)],check=True)
