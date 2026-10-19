#include "filen.h"
#include <mbedtls/gcm.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/base64.h>
#include <string.h>
#include <ctype.h>

static bool fail(FilenBytes &out,std::string &e,const char *message){
 if(!out.empty())memset(&out[0],0,out.size());
 out.clear();
 e=message;
 return false;
}
static int nibble(char c){if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
 }
static bool unhex(const std::string&s,FilenBytes&out){
 if(s.size()%2)return false;
 out.resize(s.size()/2);
 for(size_t i=0;i<out.size();i++){int a=nibble(s[2*i]),b=nibble(s[2*i+1]);
 if(a<0||b<0)return false;
 out[i]=(unsigned char)(16*a+b);
 }return true;
}
static bool unbase64(const std::string&s,FilenBytes&out){
 if(s.empty()||s.size()>3*1024*1024)return false;
 out.resize(s.size());
 size_t n=0;
 int r=mbedtls_base64_decode(&out[0],out.size(),&n,(const unsigned char*)s.data(),s.size());
 out.resize(n);
 return r==0;
}
bool filen_parse_link(const char *url,char uuid[37],char key[257]){
 uuid[0]=key[0]=0;
 if(!url)return false;
 const char*prefixes[]={"https://app.filen.io/#/d/","https://filen.io/#/d/"};
 const char*p=NULL;
 for(unsigned i=0;i<2;i++)if(!strncmp(url,prefixes[i],strlen(prefixes[i])))p=url+strlen(prefixes[i]);
 if(!p||strlen(p)<49||strlen(p)>293)return false;
 for(int i=0;i<36;i++)if(i==8||i==13||i==18||i==23){if(p[i]!='-')return false;
 }else if(nibble(p[i])<0)return false;
 if(p[36]!='#')return false;
 size_t n=strlen(p+37);
 if(n<12||n>256)return false;
 for(size_t i=0;i<n;i++){unsigned char c=p[37+i];
 if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='='))return false;
 }
 memcpy(uuid,p,36);
 uuid[36]=0;
 memcpy(key,p+37,n+1);
 return true;
}
static bool gcm(const FilenBytes&data,const FilenBytes&key,const unsigned char*iv,FilenBytes&out,std::string&e){
 if(data.size()<16||key.size()!=32)return fail(out,e,"Invalid encrypted data or key length");
 out.resize(data.size()-16+1);
 mbedtls_gcm_context ctx;
 mbedtls_gcm_init(&ctx);
 int r=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,&key[0],256);
 if(!r)r=mbedtls_gcm_auth_decrypt(&ctx,data.size()-16,iv,12,NULL,0,&data[data.size()-16],16,&data[0],&out[0]);
 mbedtls_gcm_free(&ctx);
 if(r)return fail(out,e,"Filen authentication failed: wrong key or damaged data");
 out.resize(data.size()-16);
 return true;
}
static bool cbc(const FilenBytes&data,const unsigned char*key,const unsigned char*iv,FilenBytes&out,std::string&e){
 if(data.empty()||data.size()%16)return fail(out,e,"Invalid legacy ciphertext length");
 unsigned char ivcopy[16];
 memcpy(ivcopy,iv,16);
 out.resize(data.size());
 mbedtls_aes_context c;
 mbedtls_aes_init(&c);
 int r=mbedtls_aes_setkey_dec(&c,key,256);
 if(!r)r=mbedtls_aes_crypt_cbc(&c,MBEDTLS_AES_DECRYPT,data.size(),ivcopy,&data[0],&out[0]);
 mbedtls_aes_free(&c);
 if(r)return fail(out,e,"Legacy decryption failed");
 unsigned pad=out.back();
 if(!pad||pad>16)return fail(out,e,"Invalid legacy padding/key");
 for(unsigned i=0;i<pad;i++)if(out[out.size()-1-i]!=pad)return fail(out,e,"Invalid legacy padding/key");
 out.resize(out.size()-pad);
 return true;
}
static bool legacy(FilenBytes data,const std::string&password,FilenBytes&out,std::string&e){
 if(data.size()>=8&&!memcmp(&data[0],"U2FsdGVk",8)){
  FilenBytes decoded;
 if(!unbase64(std::string(data.begin(),data.end()),decoded))return fail(out,e,"Invalid legacy base64");
 data.swap(decoded);
 }
 if(data.size()>=16&&!memcmp(&data[0],"Salted__",8)){
  unsigned char derived[48],digest[16];
 size_t used=0;
 FilenBytes input;
  while(used<48){input.clear();
 if(used)input.insert(input.end(),digest,digest+16);
 input.insert(input.end(),password.begin(),password.end());
 input.insert(input.end(),data.begin()+8,data.begin()+16);
   if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5),&input[0],input.size(),digest))return fail(out,e,"MD5 unavailable for legacy share");
 memcpy(derived+used,digest,16);
 used+=16;
 }
  FilenBytes ciphertext(data.begin()+16,data.end());
 bool ok=cbc(ciphertext,derived,derived+32,out,e);
 memset(derived,0,sizeof(derived));
 return ok;
 }
 if(password.size()!=32)return fail(out,e,"Legacy file key must be 32 bytes");
 return cbc(data,(const unsigned char*)password.data(),(const unsigned char*)password.data(),out,e);
}
bool filen_decrypt_metadata(const std::string&data,const std::string&password,FilenBytes&out,std::string&e){
 out.clear();
 if(password.empty()||data.size()>65536)return fail(out,e,"Invalid Filen metadata");
 if(data.compare(0,8,"U2FsdGVk")==0)return legacy(FilenBytes(data.begin(),data.end()),password,out,e);
 FilenBytes key,iv,encrypted;
 size_t offset=0;
 if(data.compare(0,3,"002")==0&&data.size()>=15){
  key.resize(32);
 mbedtls_md_context_t ctx;
 mbedtls_md_init(&ctx);
 int r=mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),1);
  if(!r)r=mbedtls_pkcs5_pbkdf2_hmac(&ctx,(const unsigned char*)password.data(),password.size(),(const unsigned char*)password.data(),password.size(),1,32,&key[0]);
 mbedtls_md_free(&ctx);
  if(r)return fail(out,e,"Filen key derivation failed");
 iv.assign(data.begin()+3,data.begin()+15);
 offset=15;
 }else if(data.compare(0,3,"003")==0&&data.size()>=27){
  if(password.size()!=64||!unhex(password,key)||!unhex(data.substr(3,24),iv))return fail(out,e,"Invalid Filen v3 key/nonce");
 offset=27;
 }else return fail(out,e,"Unsupported Filen metadata version");
 if(!unbase64(data.substr(offset),encrypted))return fail(out,e,"Invalid Filen metadata base64");
 return gcm(encrypted,key,&iv[0],out,e);
}
bool filen_decrypt_chunk(const FilenBytes&data,const std::string&password,int version,FilenBytes&out,std::string&e){
 out.clear();
 if(data.size()>2*1024*1024)return fail(out,e,"Oversized Filen chunk");
 if(version==1)return legacy(data,password,out,e);
 if(version!=2&&version!=3)return fail(out,e,"Unsupported Filen file version");
 FilenBytes key;
 if(version==2)key.assign(password.begin(),password.end());
 else if(password.size()!=64||!unhex(password,key))return fail(out,e,"Invalid Filen v3 key");
 if(data.size()<28)return fail(out,e,"Truncated Filen chunk");
 return gcm(FilenBytes(data.begin()+12,data.end()),key,&data[0],out,e);
}
