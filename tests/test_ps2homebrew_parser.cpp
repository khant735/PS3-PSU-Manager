#include "ps2homebrew_catalogue.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
int main(){
 const char *paths[]={"/exploits/","/loaders/","/apps/","/sys_apps/","/emulators/","/games/","/diag/","/umcs/"};
 for(int i=0;i<8;i++){char u[128];std::snprintf(u,sizeof(u),"https://ps2homebrewstore.com%s",paths[i]);assert(ps2hb_valid_category_url(u,i));assert(std::strcmp(ps2hb_categories[i].path,paths[i])==0);}
 assert(!ps2hb_valid_category_url("http://ps2homebrewstore.com/games/",5));
 assert(!ps2hb_valid_category_url("https://evilps2homebrewstore.com/games/",5));
 assert(!ps2hb_valid_category_url("https://ps2homebrewstore.com/games/extra/",5));
 assert(!ps2hb_valid_category_url("https://u@ps2homebrewstore.com/games/",5));
 assert(!ps2hb_valid_category_url("https://ps2homebrewstore.com:444/games/",5));
 assert(ps2hb_valid_entry_url("https://ps2homebrewstore.com/games/example/"));
 assert(ps2hb_valid_entry_url("https://ps2homebrewstore.com/games/example"));
 assert(!ps2hb_valid_entry_url("https://ps2homebrewstore.com/games/../x"));
 assert(ps2hb_asset_kind_from_url("https://downloads.ps2homebrewstore.com/file.PSU?x=1")==PS2HB_ASSET_PSU);
 assert(ps2hb_asset_kind_from_url("https://downloads.ps2homebrewstore.com/file.zip")==PS2HB_ASSET_ZIP);
 assert(!ps2hb_asset_kind_from_url("http://downloads.ps2homebrewstore.com/file.zip"));
 assert(!ps2hb_asset_kind_from_url("https://downloads.ps2homebrewstore.com.evil/file.zip"));
 assert(!ps2hb_asset_kind_from_url("https://example.org/file.psu"));
 assert(ps2hb_requires_verified_tls("https://ps2homebrewstore.com/games/"));
 assert(ps2hb_requires_verified_tls("https://downloads.ps2homebrewstore.com/a.psu"));
 assert(!ps2hb_requires_verified_tls("http://ps2homebrewstore.com/games/"));
 const char *html="<article><a class='item' href='/games/save-manager/'><h2>Save &amp; Backup</h2></a></article><a href='https://ps2homebrewstore.com/games/save-manager/'>duplicate</a><a href='/games/../bad/'>bad</a><a href='/games/broken><h2>broken</h2></a>";
 ps2hb_entry_t entries[3];int n=ps2hb_parse_category(html,5,entries,3);assert(n==1);assert(std::strcmp(entries[0].title,"Save & Backup")==0);assert(std::strcmp(entries[0].detail_url,"https://ps2homebrewstore.com/games/save-manager/")==0);
 const char *detail="<article><h1>Save &amp; Backup</h1><a href='https://downloads.ps2homebrewstore.com/full.psu'>Memory card PSU</a><a href='/games/file.zip'>bad relative</a><a href='https://downloads.ps2homebrewstore.com/backup.zip'>ZIP archive</a><a href='https://downloads.ps2homebrewstore.com/readme.txt'>unsupported</a><a href='http://downloads.ps2homebrewstore.com/no.zip'>HTTP</a><a href='https://evil.example/x.psu'>evil</a><a href='https://downloads.ps2homebrewstore.com/full.psu'>duplicate</a></article>";
 ps2hb_asset_t assets[4];n=ps2hb_parse_entry(detail,"Save & Backup",assets,4);assert(n==2);assert(assets[0].kind==PS2HB_ASSET_PSU);assert(std::strcmp(assets[0].label,"Memory card PSU")==0);assert(assets[1].kind==PS2HB_ASSET_ZIP);
 const char *cards="<ul><li><p>3D Pinball <img alt='non-sas-zip_pic'></p><hr><p>Both files are required.</p><p><a href='https://downloads.ps2homebrewstore.com/3dp.psu'>3D Pinball Player</a></p><p><a href='https://downloads.ps2homebrewstore.com/3dp.zip'>3D Pinball Assets</a></p></li><li><p>Hermes</p><p><a href='https://downloads.ps2homebrewstore.com/hermes.psu'>Hermes</a></p></li></ul>";
 ps2hb_entry_t card_entries[4];n=ps2hb_parse_category(cards,5,card_entries,4);assert(n==2);assert(std::strcmp(card_entries[0].title,"3D Pinball")==0);assert(std::strcmp(card_entries[0].detail_url,"https://ps2homebrewstore.com/games/")==0);
 ps2hb_asset_t card_assets[4];n=ps2hb_parse_entry(cards,"3D Pinball",card_assets,4);assert(n==2);assert(std::strcmp(card_assets[0].label,"3D Pinball Player")==0);assert(card_assets[1].kind==PS2HB_ASSET_ZIP);n=ps2hb_parse_entry(cards,"Hermes",card_assets,4);assert(n==1);assert(card_assets[0].kind==PS2HB_ASSET_PSU);
 ps2hb_entry_t one[1];n=ps2hb_parse_category("<a href='/games/a/'>A</a><a href='/games/b/'>B</a>",5,one,1);assert(n==1);
 const char *guide="<main><h1>Exploits: Which PS2 Model do you have?</h1><ul><li>SCPH-10K</li><li>You will use ProtoPwn!</li></ul><a href='https://example.org/guide'>Read the guide</a></main>";
 ps2hb_entry_t guide_entries[4];n=ps2hb_parse_category(guide,0,guide_entries,4);assert(n==0);
 puts("PASS: PS2 Homebrew Store parser and URL rules");
}
