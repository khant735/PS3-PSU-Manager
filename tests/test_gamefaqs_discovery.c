#include "../source/gamefaqs_html_native.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 char urls[4][384]={{0}};
 const char *html="<a href='/ps2/123456-example-game/saves'>A</a>"
 "<a href=\"https://gamefaqs.gamespot.com/ps2/123456-example-game/saves/987654?foo=1\">B</a>"
 "<a href='/ps2/123456-example-game/saves'>Duplicate</a>"
 "<a href='https://evil.example/ps2/123456-example-game/saves'>Unsafe</a>"
 "<a href='/ps2/123456-example-game/faqs'>Not saves</a>";
 int n=gfs_discover_save_links(html,urls,4);
 assert(n==2);
 assert(strcmp(urls[0],"https://gamefaqs.gamespot.com/ps2/123456-example-game/saves")==0);
 assert(strcmp(urls[1],"https://gamefaqs.gamespot.com/ps2/123456-example-game/saves/987654")==0);
 assert(gfs_discover_save_links("<a href=\'https://gamefaqs.gamespot.com/other/ps2/123456-example-game/saves\'>bad</a>",urls,4)==0);
 assert(gfs_discover_save_links("<a href=\'/ps2/123456-example-game/saves?x=1&amp;y=2\'>encoded</a>",urls,4)==0);
 assert(gfs_discover_save_links("",urls,4)==0);
 assert(gfs_discover_save_links(NULL,urls,4)==-1);
 assert(gfs_discover_save_links(html,urls,1)==1);
 puts("GameFAQs save link discovery OK");
 return 0;
}
