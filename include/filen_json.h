#ifndef FILEN_JSON_H
#define FILEN_JSON_H
#include <map>
#include <string>
#include <stdlib.h>
/* Bounded JSON reader for Filen responses. Reject duplicate properties and
 * malformed escapes; unknown structured fields are parsed, never scanned. */
struct FilenJson {
 char type;
 std::string text;
 std::map<std::string,FilenJson> fields;
 FilenJson():type(0){}
 const FilenJson &get(const char *key)const{static const FilenJson none;
 std::map<std::string,FilenJson>::const_iterator i=fields.find(key);
 return i==fields.end()?none:i->second;
 }
};
class FilenJsonReader {
 const char*p,*end;
 unsigned nodes;
 void ws(){while(p<end&&(*p==' '||*p=='\n'||*p=='\r'||*p=='\t'))++p;
 }
 int hex(){if(end-p<4)return -1;
 int n=0;
 for(int i=0;i<4;i++){char c=*p++;
 int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
 if(v<0)return -1;
 n=n*16+v;
 }return n;
 }
 bool str(std::string&s){if(p==end||*p++!='"')return false;
 s.clear();
 while(p<end){unsigned char c=*p++;
 if(c=='"')return true;
 if(c<32)return false;
 if(c!='\\'){s+=(char)c;
 continue;
 }if(p==end)return false;
 c=*p++;
  if(c=='"'||c=='\\'||c=='/')s+=(char)c;
 else if(c=='b')s+='\b';
 else if(c=='f')s+='\f';
 else if(c=='n')s+='\n';
 else if(c=='r')s+='\r';
 else if(c=='t')s+='\t';
 else if(c=='u'){
   int n=hex();
 if(n<0)return false;
 if(n>=0xd800&&n<=0xdbff){if(end-p<6||p[0]!='\\'||p[1]!='u')return false;
 p+=2;
 int low=hex();
 if(low<0xdc00||low>0xdfff)return false;
 n=0x10000+((n-0xd800)<<10)+low-0xdc00;
 }else if(n>=0xdc00&&n<=0xdfff)return false;
   if(n<128)s+=(char)n;
 else if(n<2048){s+=(char)(0xc0|(n>>6));
 s+=(char)(0x80|(n&63));
 }else if(n<65536){s+=(char)(0xe0|(n>>12));
 s+=(char)(0x80|((n>>6)&63));
 s+=(char)(0x80|(n&63));
 }else{s+=(char)(0xf0|(n>>18));
 s+=(char)(0x80|((n>>12)&63));
 s+=(char)(0x80|((n>>6)&63));
 s+=(char)(0x80|(n&63));
 }
  }else return false;
 }return false;
 }
 bool value(FilenJson&v,unsigned depth){ws();
 if(p==end||depth>16||++nodes>4096)return false;
 char c=*p;
  if(c=='"'){v.type='s';
 return str(v.text);
 }if(c=='{'||c=='['){v.type=c;
 ++p;
 ws();
 char close=c=='{'?'}':']';
 if(p<end&&*p==close){++p;
 return true;
 }for(;;){std::string key;
 if(c=='{'){if(!str(key)||v.fields.count(key))return false;
 ws();
 if(p==end||*p++!=':')return false;
 }FilenJson child;
 if(!value(child,depth+1))return false;
 if(c=='{')v.fields[key]=child;
 ws();
 if(p==end)return false;
 if(*p==close){++p;
 return true;
 }if(*p++!=',')return false;
 ws();
 }}
  const char*start=p;
 if(c=='t'||c=='f'||c=='n'){const char*word=c=='t'?"true":c=='f'?"false":"null";
 size_t n=c=='f'?5:4;
 if((size_t)(end-p)<n||std::string(p,n)!=word)return false;
 p+=n;
 v.type=c;
 v.text=word;
 return true;
 }
  if(*p=='-')++p;
 if(p==end)return false;
 if(*p=='0')++p;
 else{if(*p<'1'||*p>'9')return false;
 while(p<end&&*p>='0'&&*p<='9')++p;
 }
  if(p<end&&*p=='.'){++p;
 const char*q=p;
 while(p<end&&*p>='0'&&*p<='9')++p;
 if(p==q)return false;
 }
  if(p<end&&(*p=='e'||*p=='E')){++p;
 if(p<end&&(*p=='+'||*p=='-'))++p;
 const char*q=p;
 while(p<end&&*p>='0'&&*p<='9')++p;
 if(p==q)return false;
 }
  v.type='d';
 v.text.assign(start,p);
 return true;
 }
public:
 bool parse(const std::string&s,FilenJson&v){if(s.size()>65536)return false;
 p=s.data();
 end=p+s.size();
 nodes=0;
 v=FilenJson();
 if(!value(v,0))return false;
 ws();
 return p==end;
 }
};
#endif
