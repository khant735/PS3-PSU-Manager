#include "ps2wiki_catalogue.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>

/* Parse the public PS2Wiki/Jekyll HTML; no JS runtime, no HTML dependency.
 * All URLs must be checked before being used by the downloader. */
static const char *const BASE="https://ps2wiki.github.io";
static const char *const ROOT="/sas-apps-archive/";

static int has_prefix(const char *p,const char *prefix){return !strncmp(p,prefix,strlen(prefix));}
static int allowed_path(const char *path,int cat){
 if(!has_prefix(path,ROOT))return 0;
 path+=strlen(ROOT);
 if(cat==0)return has_prefix(path,"gameslistings/");
 if(cat==1)return has_prefix(path,"sysappslistings/");
 if(cat==2)return has_prefix(path,"applistings-")||has_prefix(path,"applistings/");
 if(cat==3)return has_prefix(path,"rteslistings/");
 if(cat==4)return has_prefix(path,"umcslistings/");
 if(cat==5)return has_prefix(path,"otherlistings/");
 return 0;
}
int ps2wiki_valid_detail_url(const char *url,int cat){
 if(!url || cat<0 || cat>=PS2WIKI_CATEGORY_COUNT || !has_prefix(url,BASE))return 0;
 const char *p=url+strlen(BASE);
 if(!allowed_path(p,cat))return 0;
 if(!strstr(p,".html") && p[strlen(p)-1]!='/')return 0;
 if(strstr(p,"/../")||strstr(p,"/./")||strstr(p,"..")||strstr(p,"%"))return 0;
 for(;*p;p++)if(*p=='?'||*p=='#'||*p=='\\'||*p==' '||*p=='\r'||*p=='\n')return 0;
 return 1;
}
int ps2wiki_valid_filen_url(const char *url){
 if(!url || !has_prefix(url,"https://app.filen.io/#/d/"))return 0;
 const char *p=url+strlen("https://app.filen.io/#/d/");
 if(strlen(p)<49)return 0;
 /* Filen public share UUID + fragment key, never an HTTP download URL. */
 for(int i=0;i<36;i++){
  char c=p[i];if(!c)return 0;
  if(i==8||i==13||i==18||i==23){if(c!='-')return 0;}
  else if(!isxdigit((unsigned char)c))return 0;
 }
 if(p[36]!='#')return 0;
 p+=37;int n=0;
 while(p[n] && n<=256){unsigned char c=(unsigned char)p[n];if(!(isalnum(c)||c=='-'||c=='_'||c=='='))return 0;n++;}
 return n>=12 && n<=256 && p[n]==0;
}
int ps2wiki_valid_direct_psu(const char *url){
 if(!url || (!has_prefix(url,"https://")&&!has_prefix(url,"http://")))return 0;
 const char *host=strstr(url,"://")+3;
 const char *slash=strchr(host,'/');if(!slash||slash==host)return 0;
 const char *end=slash+strlen(slash),*q=strpbrk(slash,"?#");if(q)end=q;
 if(end-slash<4 || strncasecmp(end-4,".psu",4))return 0;
 for(const char *p=url;p<end;p++)if(*p=='\\'||*p==' '||*p=='\r'||*p=='\n')return 0;
 return 1;
}
static void clean_text(const char *begin,const char *end,char *out,size_t cap){
 size_t n=0;int in_tag=0,space=0;
 for(const char *p=begin;p<end&&*p&&n+1<cap;p++){
  if(*p=='<'){in_tag=1;continue;}if(*p=='>'&&in_tag){in_tag=0;continue;}if(in_tag)continue;
  if(*p=='&'){
   const char *semi=(const char*)memchr(p,';',(size_t)(end-p)<16?(size_t)(end-p):16);
   if(semi){char c=0;size_t l=(size_t)(semi-p);
    if(l==4&&!strncmp(p,"&amp",4))c='&';
    else if(l==5&&!strncmp(p,"&quot",5))c='"';
    else if(l==5&&!strncmp(p,"&apos",5))c='\'';
    else if(l==5&&!strncmp(p,"&#039",5))c='\'';
    else if(l==4&&!strncmp(p,"&#39",4))c='\'';
    else if(l==3&&!strncmp(p,"&lt",3))c='<';
    else if(l==3&&!strncmp(p,"&gt",3))c='>';
    if(c){if(space && n+1<cap)out[n++]=' ';out[n++]=c;p=semi;space=0;continue;}
   }
  }
  unsigned char c=(unsigned char)*p;
  if(c<=32||c==127){if(n)space=1;continue;}
  if(c>=128){if(n)space=1;continue;} /* RSX font is ASCII-only. */
  if(space && n+1<cap){out[n++]=' ';space=0;}
  out[n++]=(char)c;
 }
 while(n && out[n-1]==' ')n--;
 out[n]=0;
}
static void extract_tag_class(const char *begin,const char *end,const char *klass,char *out,size_t cap){
 out[0]=0;const char *p=begin;
 while((p=strstr(p,"class=")) && p<end){
  const char *q=p+6;char quote=*q;if(quote!='"'&&quote!='\''){p=q;continue;}q++;
  const char *qe=strchr(q,quote);if(!qe||qe>=end)break;
  if((size_t)(qe-q)==strlen(klass)&&!strncmp(q,klass,strlen(klass))){
   const char *gt=strchr(qe,'>');if(!gt||gt>=end)break;
   const char *close=strstr(gt+1,"</p>");if(!close||close>=end)break;
   clean_text(gt+1,close,out,cap);return;
  }
  p=qe+1;
 }
}
static int placeholder_listing(const char *title,const char *version){
 return (title&&(!strcasecmp(title,"Coming Soon")||!strcasecmp(title,"ComingSoon")))||
        (version&&!strcasecmp(version,"WIP"));
}
static int resolve_detail(const char *src,size_t len,int cat,char *out,size_t cap){
 if(!len||len>=cap)return 0;
 char raw[PS2WIKI_DETAIL_URL];if(len>=sizeof(raw))return 0;
 memcpy(raw,src,len);raw[len]=0;
 if(has_prefix(raw,BASE))snprintf(out,cap,"%s",raw);
 else if(raw[0]=='/')snprintf(out,cap,"%s%s",BASE,raw);
 else return 0;
 return ps2wiki_valid_detail_url(out,cat);
}
int ps2wiki_parse_category(const char *html,int category,ps2wiki_entry_t *out,int cap){
 if(!html||!out||cap<=0||category<0||category>=PS2WIKI_CATEGORY_COUNT)return 0;
 int n=0;const char *p=html;
 while((p=strstr(p,"<a"))&&n<cap){
  const char *gt=strchr(p,'>');if(!gt)break;
  const char *href=strstr(p,"href=");if(!href||href>=gt){p=gt+1;continue;}
  const char *v=href+5;char quote=*v;if(quote!='"'&&quote!='\''){p=gt+1;continue;}v++;
  const char *ve=strchr(v,quote);if(!ve||ve>=gt){p=gt+1;continue;}
  char detail[PS2WIKI_DETAIL_URL];if(!resolve_detail(v,(size_t)(ve-v),category,detail,sizeof(detail))){p=gt+1;continue;}
  const char *close=strstr(gt+1,"</a>");if(!close)break;
  int dup=0;for(int i=0;i<n;i++)if(!strcmp(out[i].detail_url,detail)){dup=1;break;}
  if(!dup){
   ps2wiki_entry_t e;memset(&e,0,sizeof(e));e.category=category;
   snprintf(e.detail_url,sizeof(e.detail_url),"%s",detail);
   extract_tag_class(gt+1,close,"title is-4",e.title,sizeof(e.title));
   extract_tag_class(gt+1,close,"title is-5 has-text-right",e.version,sizeof(e.version));
   if(!e.title[0]){
    const char *tail=strrchr(detail,'/');tail=tail?tail+1:detail;
    const char *dot=strstr(tail,".html");clean_text(tail,dot?dot:tail+strlen(tail),e.title,sizeof(e.title));
   }
   if(e.title[0]&&!placeholder_listing(e.title,e.version))out[n++]=e;
  }
  p=close+4;
 }
 return n;
}
static int find_supported_url(const char *html,char *url,size_t cap){
 if(!html||!url||cap<80)return 0;
 const char *p=html;
 while((p=strstr(p,"https:"))){
  const char *q=p+6;int scheme_ok=1;
  for(int slash=0;slash<2;slash++){while(*q=='\\')q++;if(*q!='/'){scheme_ok=0;break;}q++;}
  if(!scheme_ok){p+=6;continue;}
  char candidate[PS2WIKI_DOWNLOAD_URL];size_t n=0;
  memcpy(candidate,"https://",8);n=8;
  while(*q&&n+1<sizeof(candidate)){
   if(*q=='\\'){const char *r=q;while(*r=='\\')r++;if(*r=='/'){candidate[n++]='/';q=r+1;continue;}break;}
   if(isspace((unsigned char)*q)||*q=='<'||*q=='>'||*q=='"'||*q=='\''||*q=='`'||*q==')'||*q==']'||*q=='}'||*q==','||*q==';')break;
   candidate[n++]=*q++;
  }
  candidate[n]=0;
  if(ps2wiki_valid_filen_url(candidate)||ps2wiki_valid_direct_psu(candidate)){
   if(strlen(candidate)>=cap)return 0;
   snprintf(url,cap,"%s",candidate);
   return ps2wiki_valid_filen_url(url)?2:1;
  }
  p=q>p?q:p+6;
 }
 return 0;
}
int ps2wiki_parse_detail(const char *html,char *url,size_t cap){
 if(!html||!url||cap<80)return 0;
 url[0]=0;
 /* Prefer the archive's original Jekyll resolver format when present. */
 const char *fun=strstr(html,"function download_link");
 if(fun){
  size_t remain=strlen(fun);
  const char *limit=fun+(remain<4096?remain:4096);
  const char *push=strstr(fun,"bits.push(");
  if(push&&push<limit){
   const char *p=push+10;while(p<limit&&(*p==' '||*p=='\t'))p++;
   char quote=*p;
   if(quote=='"'||quote=='\''){
    p++;const char *q=strchr(p,quote);
    if(q&&q<limit&&(size_t)(q-p)<cap){
     memcpy(url,p,(size_t)(q-p));url[q-p]=0;
     if(ps2wiki_valid_filen_url(url))return 2;
     if(ps2wiki_valid_direct_psu(url))return 1;
    }
   }
  }
 }
 /* Current SAS detail pages expose the selected version's share/download URL
    in form values or page scripts instead of the legacy bits.push() call. */
 url[0]=0;
 return find_supported_url(html,url,cap);
}
