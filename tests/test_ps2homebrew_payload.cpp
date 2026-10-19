#include "ps2homebrew_catalogue.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
static int check(const unsigned char *b,size_t n){const char *p="/tmp/ps2hb-zip-signature.bin";FILE*f=fopen(p,"wb");assert(f);fwrite(b,1,n,f);fclose(f);int r=ps2hb_validate_zip_file(p);unlink(p);return r;}
int main(){
 const unsigned char local[]={'P','K',3,4,1};const unsigned char empty[]={'P','K',5,6};const unsigned char span[]={'P','K',7,8};const unsigned char bad[]={'N','O','P','E'};const unsigned char shortf[]={'P','K',3};
 assert(check(local,sizeof(local))==0);assert(check(empty,sizeof(empty))==0);assert(check(span,sizeof(span))==0);assert(check(bad,sizeof(bad))!=0);assert(check(shortf,sizeof(shortf))!=0);
 assert(ps2hb_requires_verified_tls("https://downloads.ps2homebrewstore.com/a.psu"));assert(!ps2hb_requires_verified_tls("http://downloads.ps2homebrewstore.com/a.psu"));
 assert(ps2hb_redirect_allowed("https://downloads.ps2homebrewstore.com/a.zip"));assert(!ps2hb_redirect_allowed("https://ps2homebrewstore.com/games/foo/"));
 assert(!ps2hb_redirect_allowed("http://downloads.ps2homebrewstore.com/a.zip"));assert(!ps2hb_redirect_allowed("https://evil.example/a.zip"));assert(!ps2hb_redirect_allowed("https://downloads.ps2homebrewstore.com/a.exe"));
 puts("PASS: PS2 Homebrew Store payload signatures and transport policy");
}
