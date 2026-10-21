#include "ps2_game_db_update.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <string>
#include <map>
#include <vector>

/* Some PS3 filesystems reject rename() when the destination exists.
 * Keep the previous DB in .bak until the new file has been installed.
 * Never overwrite a stranded backup: it may be the only recoverable copy.
 */
static int replace_preserving_old(const std::string &temp,const std::string &target){
 errno=0;
 if(rename(temp.c_str(),target.c_str())==0)return 0;
 if(errno!=EEXIST)return -1;
 std::string backup=target+".bak";
 FILE *check=fopen(backup.c_str(),"rb");
 if(check){fclose(check);errno=EEXIST;return -1;}
 if(rename(target.c_str(),backup.c_str())!=0)return -1;
 if(rename(temp.c_str(),target.c_str())==0){
  /* A failed cleanup leaves a harmless backup, never delete the new DB. */
  remove(backup.c_str());
  return 0;
 }
 int install_error=errno;
 if(rename(backup.c_str(),target.c_str())!=0){
  /* Leave both the staged new file and the old .bak for manual recovery. */
  errno=EIO;
  return -2;
 }
 errno=install_error;
 return -1;
}
static const char *GAME_URL="https://github.com/niemasd/GameDB-PS2/releases/latest/download/PS2.titles.json";
static const char *REDUMP_URL="https://redump.info/datfile/ps2/serial,version";
struct Buffer {std::string data;size_t max;};
static size_t collect(char *ptr,size_t size,size_t count,void *ctx){
 Buffer *b=(Buffer*)ctx;size_t n=size*count;
 if(n>b->max || b->data.size()>b->max-n)return 0;
 b->data.append(ptr,n);return n;
}
static char last_download_error[192];
static int download(const char *url,std::string &out,size_t limit){
 last_download_error[0]=0;
 CURL *c=curl_easy_init();if(!c){snprintf(last_download_error,sizeof(last_download_error),"curl initialization failed");return -1;}
 Buffer b;b.max=limit;char curl_error[CURL_ERROR_SIZE]={0};
 curl_easy_setopt(c,CURLOPT_ERRORBUFFER,curl_error);
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,collect);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,180L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"PS3-PSU-Manager/1.63T GameDB-Updater");
 /* Same installed CA bundle as the existing HTTPS downloader. */
 curl_easy_setopt(c,CURLOPT_CAINFO,"/dev_hdd0/game/PSU000001/USRDIR/cacert.pem");
 CURLcode code=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
 curl_easy_cleanup(c);if(code!=CURLE_OK||http!=200){snprintf(last_download_error,sizeof(last_download_error),"HTTP %ld, curl %d: %.110s",http,(int)code,curl_error[0]?curl_error:curl_easy_strerror(code));return -1;}out.swap(b.data);return 0;
}
static bool serial(const std::string &s){if(s.size()!=10||s[4]!='-')return false;
 for(int i=0;i<4;i++)if(!isalpha((unsigned char)s[i]))return false;
 for(int i=5;i<10;i++)if(!isdigit((unsigned char)s[i]))return false;return true;}
static void ws(const std::string &s,size_t &p){while(p<s.size()&&isspace((unsigned char)s[p]))p++;}
static bool quoted(const std::string &s,size_t &p,std::string &v){
 ws(s,p);if(p>=s.size()||s[p++]!='"')return false;v.clear();
 while(p<s.size()){unsigned char c=s[p++];if(c=='"')return true;
 if(c=='\\'){if(p>=s.size())return false;char e=s[p++];if(e=='u'){if(p+4>s.size())return false;p+=4;v+='?';continue;}
 switch(e){case 'n':case 'r':case 't':v+=' ';break;default:v+=e;}continue;}
 if(c<32)return false;v+=(char)c;
 }return false;
}
static bool parse_titles(const std::string &s,std::map<std::string,std::string> &titles){
 size_t p=0;ws(s,p);if(p>=s.size()||s[p++]!='{')return false;
 while(p<s.size()){
 ws(s,p);if(p>=s.size())return false;if(s[p]=='}'){p++;ws(s,p);return p==s.size();}
 std::string key,val;if(!quoted(s,p,key))return false;ws(s,p);
 if(p>=s.size()||s[p++]!=':')return false;ws(s,p);
 if(p>=s.size()||s[p]!='"')return false; // reject unknown schema, never corrupt existing DB
 if(!quoted(s,p,val))return false;
 for(size_t i=0;i<key.size();i++)key[i]=(char)toupper((unsigned char)key[i]);
 if(serial(key)&&!val.empty()&&val.size()<512)titles[key]=val;
 ws(s,p);if(p>=s.size())return false;
 if(s[p]==','){p++;ws(s,p);if(p>=s.size()||s[p]=='}')return false;continue;}if(s[p]=='}'){p++;ws(s,p);return p==s.size();}return false;
 }return false;
}
/* Redump serials may use underscores or omit the dash; normalize only
   known four-letter/five-digit PS2 serial shapes. */
static std::string redump_serial_at(const std::string &name,size_t pos){
 if(pos+10>name.size())return std::string();
 std::string id=name.substr(pos,10);
 if(id[4]=='_')id[4]='-';
 for(size_t j=0;j<4;j++)id[j]=(char)toupper((unsigned char)id[j]);
 if(!serial(id))return std::string();
 if(pos>0&&isalnum((unsigned char)name[pos-1]))return std::string();
 if(pos+10<name.size()&&isalnum((unsigned char)name[pos+10]))return std::string();
 return id;
}
static void merge_redump_xml(const std::string &s,std::map<std::string,std::string> &titles,std::map<std::string,std::string> &sources){
 // Redump names may contain serial in parentheses; keep existing GameDB names authoritative.
 size_t pos=0;while((pos=s.find("<game ",pos))!=std::string::npos){
 size_t end=s.find('>',pos);if(end==std::string::npos)break;
 size_t attr=s.find("name=\"",pos);if(attr==std::string::npos||attr>end){pos=end+1;continue;}
 attr+=6;size_t q=s.find('"',attr);if(q==std::string::npos||q>end){pos=end+1;continue;}
 std::string name=s.substr(attr,q-attr);pos=end+1;
 for(size_t i=0;i+10<=name.size();i++){
 std::string id=redump_serial_at(name,i);if(!id.empty()&&titles.find(id)==titles.end()){
 titles[id]=name;sources[id]="Redump";break;
 }
 }
 }
}
extern "C" int ps2_game_db_update(const char *target,char *message,size_t cap){
 if(message&&cap)message[0]=0;
 if(!target)return -1;
 std::string json;std::map<std::string,std::string> titles;
 std::map<std::string,std::string> sources;
 if(download(GAME_URL,json,16*1024*1024)!=0){
 if(message&&cap)snprintf(message,cap,"GameDB download failed: %s; previous DB preserved",last_download_error);return -1;
 }
 if(!parse_titles(json,titles)||titles.size()<1000){
 if(message&&cap)snprintf(message,cap,"GameDB JSON schema/count invalid (%lu titles); previous DB preserved",(unsigned long)titles.size());return -1;
 }
 for(std::map<std::string,std::string>::const_iterator it=titles.begin();it!=titles.end();++it)sources[it->first]="GameDB";
 size_t primary=titles.size();std::string xml;
 if(download(REDUMP_URL,xml,32*1024*1024)==0&&xml.find("<game ")!=std::string::npos)merge_redump_xml(xml,titles,sources);
 // Never shrink the database on upstream errors or removals.
 FILE *old=fopen(target,"rb");if(old){char line[2048];while(fgets(line,sizeof(line),old)){
 char *tab=strchr(line,'\t');if(!tab)continue;*tab=0;
 std::string id(line),title(tab+1);while(!title.empty()&&(title[title.size()-1]=='\n'||title[title.size()-1]=='\r'))title.resize(title.size()-1);
 if(serial(id)&&!title.empty()&&titles.find(id)==titles.end()){titles[id]=title;sources[id]="Local";}
 }fclose(old);}
 std::string temp=std::string(target)+".part";
 FILE *f=fopen(temp.c_str(),"wb");if(!f){if(message&&cap)snprintf(message,cap,"Cannot open database temporary file");return -1;}
 bool ok=true;for(std::map<std::string,std::string>::const_iterator it=titles.begin();it!=titles.end();++it){
 std::string name=it->second;for(size_t i=0;i<name.size();i++)if(name[i]=='\t'||name[i]=='\r'||name[i]=='\n')name[i]=' ';
 if(fprintf(f,"%s\t%s\n",it->first.c_str(),name.c_str())<0){ok=false;break;}
 }
 if(fflush(f)!=0)ok=false;if(fclose(f)!=0)ok=false;
 if(!ok){
  int err=errno;
  remove(temp.c_str());
  if(message&&cap)snprintf(message,cap,"DB write/flush failed (errno %d); previous DB preserved",err);
  return -1;
 }
 int replacement=replace_preserving_old(temp,std::string(target));
 if(replacement!=0){
  int err=errno;
  if(replacement!=-2)remove(temp.c_str());
  if(message&&cap)snprintf(message,cap,replacement==-2?
   "DB install/rollback failed (errno %d); inspect .bak and .part":
   "DB replace failed (errno %d); previous DB preserved",err);
  return -1;
 }
 /* Build a per-serial release index locally. Preserve existing enriched records
    so a routine GameDB refresh cannot erase verified release metadata. */
 std::string releases=std::string(target);
 size_t dot=releases.rfind(".tsv");
 if(dot!=std::string::npos)releases.replace(dot,4,".releases.tsv");
 else releases+=".releases.tsv";
 std::map<std::string,std::string> previous;
 FILE *existing=fopen(releases.c_str(),"rb");
 if(existing){
  char line[4096];
  while(fgets(line,sizeof(line),existing)){
   if(line[0]=='#')continue;
   char *tab=strchr(line,'\t');
   if(!tab)continue;
   std::string id(line,(size_t)(tab-line));
   if(serial(id)){
    std::string row(line);
    if(row.find('\n')==std::string::npos)continue;
    /* Only preserve structurally valid eight-column release rows. */
    unsigned tabs=0;for(size_t j=0;j<row.size();j++)if(row[j]=='\t')tabs++;
    if(tabs==7)previous[id]=row;
   }
  }
  fclose(existing);
 }
 std::string release_tmp=releases+".part";
 FILE *rf=fopen(release_tmp.c_str(),"wb");
 bool release_written=false;
 if(rf){
  bool rok=fputs("# serial\ttitle\tregion\tedition\trevision\trelease_date\tdate_precision\tsource\n",rf)>=0;
  for(std::map<std::string,std::string>::const_iterator it=titles.begin();rok&&it!=titles.end();++it){
   std::map<std::string,std::string>::const_iterator old=previous.find(it->first);
   if(old!=previous.end()){
    if(fputs(old->second.c_str(),rf)<0)rok=false;
   }else{
    std::string name=it->second;
    for(size_t i=0;i<name.size();i++)if(name[i]=='\t'||name[i]=='\r'||name[i]=='\n')name[i]=' ';
    if(fprintf(rf,"%s\t%s\t\t\t\t\tunknown\t%s\n",it->first.c_str(),name.c_str(),sources[it->first].c_str())<0)rok=false;
   }
  }
  /* Preserve records for serials no longer returned by an upstream source. */
  for(std::map<std::string,std::string>::const_iterator it=previous.begin();rok&&it!=previous.end();++it){
   if(titles.find(it->first)==titles.end()&&fputs(it->second.c_str(),rf)<0)rok=false;
  }
  if(fflush(rf)!=0)rok=false;
  if(fclose(rf)!=0)rok=false;
  if(rok&&replace_preserving_old(release_tmp,releases)==0)release_written=true;
  else if(errno!=EIO)remove(release_tmp.c_str());
 }
 if(message&&cap){
  if(release_written)snprintf(message,cap,"%lu titles (%lu GameDB); release index updated",(unsigned long)titles.size(),(unsigned long)primary);
  else snprintf(message,cap,"%lu titles updated; release index write failed (old index preserved)",(unsigned long)titles.size());
 }
 return (int)titles.size();
}
