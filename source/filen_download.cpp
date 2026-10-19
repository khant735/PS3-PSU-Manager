#include "filen.h"
#include "filen_json.h"
#include <curl/curl.h>
#include <mbedtls/md.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

namespace {
const unsigned CHUNK=1048576;
struct Transfer {
 FilenBytes bytes;
 size_t limit;
 FilenProgress callback;
 void*context;
 std::string name;
 double done,total;
 bool cancelled;
 Transfer():limit(65536),callback(NULL),context(NULL),done(0),total(0),cancelled(false){}
};
size_t receive(void*p,size_t size,size_t n,void*ctx){Transfer*t=(Transfer*)ctx;
 if(size&&n>t->limit/size)return 0;
 size_t count=size*n;
 if(count>t->limit-t->bytes.size())return 0;
 unsigned char*b=(unsigned char*)p;
 t->bytes.insert(t->bytes.end(),b,b+count);
 return count;
 }
int progress(void*ctx,double,double now,double,double){Transfer*t=(Transfer*)ctx;
 double displayed=t->done+now;
 if(t->total>0&&displayed>t->total)displayed=t->total;
 if(t->callback&&t->callback(t->context,t->name.c_str(),displayed,t->total)){t->cancelled=true;
 return 1;
 }return 0;
 }
bool request(const std::string&url,const char*body,const char*ca,Transfer&t,std::string&error){
 t.bytes.clear();
 CURL*c=curl_easy_init();
 if(!c){error="Cannot initialise Filen HTTPS";
 return false;
 }
 curl_easy_setopt(c,CURLOPT_URL,url.c_str());
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 if(ca&&*ca)curl_easy_setopt(c,CURLOPT_CAINFO,ca);
 curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,90L);
 curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.50-Filen");
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,&t);
 curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
 curl_easy_setopt(c,CURLOPT_PROGRESSFUNCTION,progress);
 curl_easy_setopt(c,CURLOPT_PROGRESSDATA,&t);
 struct curl_slist*headers=NULL;
 if(body){headers=curl_slist_append(headers,"Content-Type: application/json");
 curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
 curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);
 }
 CURLcode r=curl_easy_perform(c);
 long status=0;
 curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
 curl_slist_free_all(headers);
 curl_easy_cleanup(c);
 if(t.cancelled){error="Filen cancelled; completed encrypted chunks kept";
 return false;
 }
 if(r!=CURLE_OK){error=std::string("Filen HTTPS: ")+curl_easy_strerror(r);
 return false;
 }
 if(status!=200){char msg[96];
 snprintf(msg,sizeof(msg),"Filen HTTP %ld (share unavailable or access denied)",status);
 error=msg;
 return false;
 }return true;
}
std::string hex(const unsigned char*p,size_t n){static const char*h="0123456789abcdef";
 std::string s;
 for(size_t i=0;i<n;i++){s+=h[p[i]>>4];
 s+=h[p[i]&15];
 }return s;
 }
bool unsigned_number(const std::string&s,unsigned long long&n){if(s.empty()||s.size()>12)return false;
 n=0;
 for(size_t i=0;i<s.size();i++){if(s[i]<'0'||s[i]>'9')return false;
 n=n*10+s[i]-'0';
 }return true;
 }
bool component(const std::string&s,size_t max){if(s.empty()||s.size()>max||s=="."||s=="..")return false;
 for(size_t i=0;i<s.size();i++){char c=s[i];
 if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
 }return true;
 }
bool filename(const std::string&s){if(s.empty()||s.size()>180||s=="."||s==".."||s[0]=='.')return false;
 for(size_t i=0;i<s.size();i++){unsigned char c=s[i];
 if(c<32||c==127||c=='/'||c=='\\'||c==':')return false;
 }return true;
 }
bool read_cache(const std::string&p,FilenBytes&out){struct stat st;
 if(stat(p.c_str(),&st)||st.st_size<=0||st.st_size>2*1024*1024)return false;
 FILE*f=fopen(p.c_str(),"rb");
 if(!f)return false;
 out.resize((size_t)st.st_size);
 bool ok=fread(&out[0],1,out.size(),f)==out.size();
 fclose(f);
 return ok;
 }
bool write_file(const std::string&p,const unsigned char*b,size_t size){FILE*f=fopen(p.c_str(),"wb");
 if(!f)return false;
 bool ok=fwrite(b,1,size,f)==size;
 if(fclose(f))ok=false;
 return ok;
 }
int run(const char*url,const char*dir,const char*ca,FilenProgress cb,void*ctx,FilenValidate validate,std::string&saved,std::string&e){
 char share[37],rawkey[257];
 if(!filen_parse_link(url,share,rawkey)){e="Invalid Filen public file link";
 return -1;
 }
 std::string key=rawkey;
 memset(rawkey,0,sizeof(rawkey));
 Transfer t;
 t.callback=cb;
 t.context=ctx;
 t.name="Resolving encrypted Filen share";
 unsigned char sha512[64],sha1[20];
 if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),(const unsigned char*)"empty",5,sha512)){e="SHA512 unavailable";
 return -1;
 }
 std::string digest=hex(sha512,64);
 if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1),(const unsigned char*)digest.data(),digest.size(),sha1)){e="SHA1 unavailable";
 return -1;
 }
 std::string body=std::string("{\"uuid\":\"")+share+"\",\"password\":\""+hex(sha1,20)+"\"}";
 if(!request("https://gateway.filen.io/v3/file/link/info",body.c_str(),ca,t,e))return -1;
 FilenJson root;
 FilenJsonReader parser;
 if(!parser.parse(std::string(t.bytes.begin(),t.bytes.end()),root)||root.type!='{'){e="Invalid Filen API response";
 return -1;
 }
 if(root.get("status").type!='t'){e="Filen share unavailable, expired, or password protected";
 return -1;
 }
 const FilenJson&d=root.get("data");
 if(d.type!='{'){e="Filen file metadata missing";
 return -1;
 }
 if(d.get("downloadBtn").type=='f'){e="Downloads disabled by Filen share owner";
 return -1;
 }
 const char*required[]={"name","size","uuid","region","bucket"};
 for(unsigned i=0;i<5;i++)if(d.get(required[i]).type!='s'){e="Invalid Filen file metadata field";
 return -1;
 }
 FilenBytes clear;
 std::string name,size;
 if(!filen_decrypt_metadata(d.get("name").text,key,clear,e))return -1;
 name.assign(clear.begin(),clear.end());
 if(!filen_decrypt_metadata(d.get("size").text,key,clear,e))return -1;
 size.assign(clear.begin(),clear.end());
 unsigned long long total=0,chunks=0;
 if(!unsigned_number(size,total)||!total||total>64ull*1024*1024*1024||d.get("chunks").type!='d'||!unsigned_number(d.get("chunks").text,chunks)||chunks!=(total+CHUNK-1)/CHUNK){e="Invalid Filen size/chunk count";
 return -1;
 }
 std::string version=d.get("version").text;
 int v=version=="1"||version=="1.5"?1:version=="2"?2:version=="3"?3:0;
 if(!v||d.get("version").type!='d'){e="Unsupported Filen file encryption version";
 return -1;
 }
 std::string uuid=d.get("uuid").text,region=d.get("region").text,bucket=d.get("bucket").text;
 if(!filename(name)||!component(region,64)||!component(bucket,64)||!component(uuid,36)||uuid.size()!=36){e="Unsafe Filen filename or storage identifier";
 return -1;
 }
 std::string base=std::string(dir)+"/",cache=base+".filen-"+uuid,part=base+"filen-"+share+".part",side=part+".url";
 if(base.size()+name.size()+20>=1024){e="Filen destination path too long";
 return -1;
 }
 if(mkdir(cache.c_str(),0700)&&errno!=EEXIST){e="Cannot create Filen chunk cache";
 return -1;
 }
 if(!write_file(side,(const unsigned char*)url,strlen(url))){e="Cannot save Filen resume link";
 return -1;
 }
 FILE*out=fopen(part.c_str(),"wb");
 if(!out){e="Cannot create Filen partial file";
 return -1;
 }
 t.name=name;
 t.total=(double)total;
 t.done=0;
 t.limit=2*1024*1024;
 bool ok=true;
 for(unsigned long long i=0;i<chunks;i++){
  char index[32];
 snprintf(index,sizeof(index),"%llu",i);
 std::string cp=cache+"/"+index;
 FilenBytes encrypted;
  size_t expected=(size_t)((total-i*CHUNK)>CHUNK?CHUNK:(total-i*CHUNK));
  if(progress(&t,0,0,0,0)){e="Filen cancelled; completed encrypted chunks kept";
 ok=false;
 break;
 }
  bool cached=read_cache(cp,encrypted)&&filen_decrypt_chunk(encrypted,key,v,clear,e)&&clear.size()==expected;
  if(!cached){
   bool fetched=false;
 for(int retry=0;retry<3&&!t.cancelled;retry++){
    const char*hosts[]={"https://egest.filen.io/","https://egest.filen.net/","https://egest.filen-1.net/"};
    if(request(std::string(hosts[retry])+region+"/"+bucket+"/"+uuid+"/"+index,NULL,ca,t,e)&&filen_decrypt_chunk(t.bytes,key,v,clear,e)&&clear.size()==expected){fetched=true;
 break;
 }
   }
   if(!fetched){if(e.empty())e="Filen decrypted chunk size mismatch";
 ok=false;
 break;
 }
   std::string temp=cp+".tmp";
 if(!write_file(temp,&t.bytes[0],t.bytes.size())||rename(temp.c_str(),cp.c_str())){e="Cannot save Filen chunk (disk full?)";
 unlink(temp.c_str());
 ok=false;
 break;
 }
  }
  if(fwrite(&clear[0],1,clear.size(),out)!=clear.size()){e="Cannot write decrypted file (disk full?)";
 ok=false;
 break;
 }
  t.done+=(double)clear.size();
 }
 if(fclose(out)){e="Cannot finish Filen partial file";
 ok=false;
 }
 if(!ok)return -1;
 if(validate&&validate(part.c_str())!=0){e="Filen download is not a valid PSU; completed file not installed";
 unlink(part.c_str());
 return -1;
 }
 saved=base+name;
 struct stat st;
 unsigned suffix=1;
 while(stat(saved.c_str(),&st)==0&&suffix<10000){char n[24];
 snprintf(n,sizeof(n)," (%u)",suffix++);
 size_t dot=name.rfind('.');
 saved=base+(dot==std::string::npos?name+std::string(n):name.substr(0,dot)+n+name.substr(dot));
 }
 if(stat(saved.c_str(),&st)==0){e="No free destination filename";
 return -1;
 }
 if(rename(part.c_str(),saved.c_str())){e="Cannot install completed Filen file";
 return -1;
 }
 unlink(side.c_str());
 for(unsigned long long i=0;i<chunks;i++){char n[32];
 snprintf(n,sizeof(n),"%llu",i);
 unlink((cache+"/"+n).c_str());
 }rmdir(cache.c_str());
 if(cb)cb(ctx,name.c_str(),(double)total,(double)total);
 return 0;
}
}
int filen_download(const char*url,const char*directory,const char*ca,FilenProgress progress,void*context,FilenValidate validate,char*saved,size_t saved_cap,char*error,size_t error_cap){
 std::string path,message;
 int r=run(url,directory,ca,progress,context,validate,path,message);
 if(saved&&saved_cap)snprintf(saved,saved_cap,"%s",path.c_str());
 if(error&&error_cap)snprintf(error,error_cap,"%s",message.c_str());
 return r;
}
