#include "gamefaqs_saves_index.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 static gfs_save_t e[8];
 const char *data="Game One\tEurope\tMAX\t100% completed\thttps://gamefaqs.gamespot.com/ps2/123/saves\thttps://gamefaqs.gamespot.com/files/save.max\n"
 "Bad\tEurope\tZIP\tUnsafe\thttps://evil.example/saves\thttps://evil.example/a.zip\n"
 "Game Two\tJapan\tXPS\tPage only\thttps://gamefaqs.gamespot.com/ps2/456/saves\t\n";
 assert(gfs_parse_index(data,e,8)==2);
 assert(gfs_bucket("Game One")==7&&gfs_bucket("2002 Game")==0);
 assert(gfs_download_allowed(e[0].download_url));
 assert(!gfs_download_allowed("https://gamefaqs.gamespot.com.evil.example/save.max"));
 assert(!gfs_download_allowed("http://gamefaqs.gamespot.com/save.max"));
 assert(!gfs_download_allowed("https://gamefaqs.gamespot.com/save.exe"));
 unsigned game=0,save=0;
 assert(gfs_parse_save_page_ids("https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves",&game,&save)&&game==561292&&save==0);
 assert(gfs_parse_save_page_ids("https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves/30435",&game,&save)&&game==561292&&save==30435);
 assert(!gfs_parse_save_page_ids("https://evil.example/ps2/561292-silent-hill-3/saves/30435",&game,&save));
 assert(!gfs_download_allowed("https://gamefaqs.gamespot.com/ps2/561292-silent-hill-3/saves/30435"));
 puts("PASS: index parsing, buckets, direct URL restrictions");
 return 0;
}
