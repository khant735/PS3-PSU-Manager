#include "gamefaqs_html_native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 gfs_save_t entries[8];
 const char *page="https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves/30435";
 const char *html="<a href=\"/downloads/save.max\">Save</a><a href='https://evil.example/a.max'>bad</a>"
 "<a href=\"/downloads/save.max\">Duplicate</a><a href='/ps2/561292-silent-hill-3/saves/30435'>Detail</a>";
 int n=gfs_extract_detail_html(html,page,"Silent Hill 3","Europe",entries,8);
 assert(n==1);assert(!strcmp(entries[0].format,"MAX"));
 assert(!strcmp(entries[0].download_url,"https://gamefaqs.gamespot.com/downloads/save.max"));
 assert(gfs_extract_detail_html(html,"https://evil.example", "x","y",entries,8)==-1);
 puts("PASS native HTML parser: valid, duplicate, unsafe URL, page IDs");return 0;
}
