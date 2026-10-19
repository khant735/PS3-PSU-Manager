/* Exercise the real downloader with a deterministic in-memory HTTPS transport.
 * No network or TLS claim is made by this test. */
#include "filen.h"
#include <curl/curl.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static std::string fixtures;
static int fail_chunk=-1,gets=0,reject_psu=0;
struct Mock {std::string url;void*write_data,*progress_data;curl_write_callback write;curl_progress_callback progress;long peer,host,follow;Mock():write_data(NULL),progress_data(NULL),write(NULL),progress(NULL),peer(0),host(0),follow(1){}};
extern "C" {
CURL* curl_easy_init(){return (CURL*)new Mock;}
void curl_easy_cleanup(CURL*c){delete (Mock*)c;}
CURLcode curl_easy_setopt(CURL*c,CURLoption o,...){Mock*m=(Mock*)c;va_list a;va_start(a,o);
 switch(o){case CURLOPT_URL:m->url=va_arg(a,const char*);break;case CURLOPT_WRITEDATA:m->write_data=va_arg(a,void*);break;case CURLOPT_PROGRESSDATA:m->progress_data=va_arg(a,void*);break;case CURLOPT_WRITEFUNCTION:m->write=va_arg(a,curl_write_callback);break;case CURLOPT_PROGRESSFUNCTION:m->progress=va_arg(a,curl_progress_callback);break;case CURLOPT_SSL_VERIFYPEER:m->peer=va_arg(a,long);break;case CURLOPT_SSL_VERIFYHOST:m->host=va_arg(a,long);break;case CURLOPT_FOLLOWLOCATION:m->follow=va_arg(a,long);break;default:break;}va_end(a);return CURLE_OK;}
CURLcode curl_easy_getinfo(CURL*,CURLINFO info,...){va_list a;va_start(a,info);*va_arg(a,long*)=200;va_end(a);return CURLE_OK;}
CURLcode curl_easy_perform(CURL*c){Mock*m=(Mock*)c;assert(m->peer==1&&m->host==2&&m->follow==0);
 std::string path=fixtures+"/info.json";
 if(m->url.find("/v3/file/link/info")==std::string::npos){int n=atoi(m->url.substr(m->url.rfind('/')+1).c_str());++gets;if(n==fail_chunk)return CURLE_RECV_ERROR;char b[32];snprintf(b,sizeof(b),"/%d",n);path=fixtures+b;}
 FILE*f=fopen(path.c_str(),"rb");assert(f);unsigned char b[16384];size_t n,total=0;
 while((n=fread(b,1,sizeof(b),f))){if(m->progress&&m->progress(m->progress_data,0,(double)total,0,0)){fclose(f);return CURLE_ABORTED_BY_CALLBACK;}if(m->write((char*)b,1,n,m->write_data)!=n){fclose(f);return CURLE_WRITE_ERROR;}total+=n;}fclose(f);return CURLE_OK;
}
}
static int validate(const char*){return reject_psu;}
static bool cancel=false;
static int ui(void*,const char*,double now,double){return cancel&&now>100000;}
static bool same(const std::string&a,const std::string&b){FILE*x=fopen(a.c_str(),"rb"),*y=fopen(b.c_str(),"rb");assert(x&&y);int p,q;do{p=fgetc(x);q=fgetc(y);if(p!=q){fclose(x);fclose(y);return false;}}while(p!=EOF);fclose(x);fclose(y);return true;}
int main(int argc,char**argv){assert(argc==3);fixtures=argv[1];const char*dir=argv[2];char path[1024],error[256];
 const char*url="https://app.filen.io/#/d/11111111-1111-1111-1111-111111111111#0123456789abcdefghijklmnopqrstuv";
 fail_chunk=1;assert(filen_download(url,dir,NULL,ui,NULL,validate,path,sizeof(path),error,sizeof(error))<0);assert(gets==4);
 fail_chunk=-1;gets=0;assert(!filen_download(url,dir,NULL,ui,NULL,validate,path,sizeof(path),error,sizeof(error)));assert(gets==1);assert(same(path,fixtures+"/plain"));
 // Existing destination must survive and a suffixed file must be created.
 assert(!filen_download(url,dir,NULL,ui,NULL,validate,path,sizeof(path),error,sizeof(error)));assert(std::string(path).find(" (1).psu")!=std::string::npos);assert(same(path,fixtures+"/plain"));
 reject_psu=1;assert(filen_download(url,dir,NULL,ui,NULL,validate,path,sizeof(path),error,sizeof(error))<0);assert(strstr(error,"not a valid PSU"));
 reject_psu=0;cancel=true;assert(filen_download(url,dir,NULL,ui,NULL,validate,path,sizeof(path),error,sizeof(error))<0);
 puts("Native downloader: interrupted transfer, cached resume, exact bytes, no overwrite, validation rejection, cancellation PASS");return 0;}
