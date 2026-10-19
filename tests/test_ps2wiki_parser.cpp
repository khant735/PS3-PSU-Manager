#include "ps2wiki_catalogue.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(){
 const char *pinball="https://app.filen.io/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023#741JqXO5MJZ6K7a9dLTTf1Rp6v1Qkh3s";
 const char *game="<div><a href=\"/sas-apps-archive/gameslistings/3D-Pinball-Space-Cadet/3D-Pinball-Space-Cadet.html\"><div><p class=\"title is-4\">3D Pinball Space Cadet</p><p class=\"subtitle is-4\">Info</p><p class=\"title is-5 has-text-right\">v0.3</p></div></a>"
 "<a href=\"/sas-apps-archive/gameslistings/Hermes/Hermes.html\"><p class=\"title is-4\">Hermes</p><p class=\"title is-5 has-text-right\">v1.01</p></a></div>";
 ps2wiki_entry_t es[8]={};assert(ps2wiki_parse_category(game,0,es,8)==2);
 assert(!strcmp(es[0].title,"3D Pinball Space Cadet"));assert(!strcmp(es[0].version,"v0.3"));
 assert(!strcmp(es[1].title,"Hermes"));
 assert(ps2wiki_parse_category(game,1,es,8)==0);
 const char *app="<a href=\"/sas-apps-archive/applistings-utilities/Apollo-Save-Tool/Apollo-Save-Tool.html\"><p class=\"title is-4\">Apollo &amp; Tools</p><p class=\"title is-5 has-text-right\">v1.0.2</p></a>";
 assert(ps2wiki_parse_category(app,2,es,8)==1);
 assert(!strcmp(es[0].title,"Apollo & Tools"));
 const char *rtes="<a href=\"/sas-apps-archive/rteslistings/Enceladus/Enceladus.html\"><p class=\"title is-4\">Enceladus</p><p class=\"title is-5 has-text-right\">c56803fc</p></a>";
 assert(ps2wiki_parse_category(rtes,3,es,8)==1);assert(!strcmp(es[0].title,"Enceladus"));
 const char *placeholder="<a href=\"/sas-apps-archive/otherlistings/ComingSoon/ComingSoon.html\"><p class=\"title is-4\">Coming Soon</p><p class=\"title is-5 has-text-right\">WIP</p></a>";
 assert(ps2wiki_parse_category(placeholder,5,es,8)==0);
 assert(ps2wiki_parse_category("<p>This page is work in Progress!</p>",4,es,8)==0);
 const char *detail="<script>function download_link() { var bits = [\"\"]; bits.push(\"https://app.filen.io/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023#741JqXO5MJZ6K7a9dLTTf1Rp6v1Qkh3s\"); var url=bits.join(\"\");}</script>";
 char url[1024];assert(ps2wiki_parse_detail(detail,url,sizeof(url))==2);
 assert(!strcmp(url,pinball));
 assert(ps2wiki_valid_filen_url(pinball));
 assert(!ps2wiki_valid_filen_url("https://app.filen.io/#/d/"));
 assert(!ps2wiki_valid_filen_url("https://app.filen.io/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023"));
 assert(ps2wiki_valid_direct_psu("https://example.com/path/test.psu?x=2"));
 assert(!ps2wiki_valid_direct_psu(pinball));
 assert(!ps2wiki_valid_direct_psu("https://example.com/download.html"));
 assert(!ps2wiki_valid_detail_url("https://evil.example/sas-apps-archive/gameslistings/x.html",0));
 assert(!ps2wiki_valid_detail_url("https://ps2wiki.github.io.evil.example/sas-apps-archive/gameslistings/x.html",0));
 assert(ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/sysappslistings/test.html",1));
 assert(!ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/sysappslistings/test.html",0));
 assert(ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/rteslistings/Enceladus/Enceladus.html",3));
 assert(ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/umcslistings/ComingSoon.html",4));
 assert(ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/otherlistings/example.html",5));
 assert(!ps2wiki_valid_detail_url("https://ps2wiki.github.io/sas-apps-archive/gameslistings/../../foo.html",0)); // TODO strict traversal
 const char *direct="<script>function download_link() { var bits = [\"\"]; bits.push('https://files.example.org/game.psu'); }</script>";
 assert(ps2wiki_parse_detail(direct,url,sizeof(url))==1);
 assert(!strcmp(url,"https://files.example.org/game.psu"));
 assert(ps2wiki_parse_detail("<a href='https://app.filen.io/#/d/xyz'>Download</a>",url,sizeof(url))==0);
 const char *modern_page="<select><option value=\"https://app.filen.io/#/d/4c614ee3-fb09-4765-8dc2-0656508ad023#741JqXO5MJZ6K7a9dLTTf1Rp6v1Qkh3s\">uLaunchELF v4.42d</option></select><button>Download</button>";
 assert(ps2wiki_parse_detail(modern_page,url,sizeof(url))==2);assert(!strcmp(url,pinball));
 const char *escaped_page="<script>const href=\"https:\\\\/\\\\/app.filen.io\\\\/#\\\\/d\\\\/4c614ee3-fb09-4765-8dc2-0656508ad023#741JqXO5MJZ6K7a9dLTTf1Rp6v1Qkh3s\";</script>";
 assert(ps2wiki_parse_detail(escaped_page,url,sizeof(url))==2);assert(!strcmp(url,pinball));
 puts("PS2Wiki category and download-link parser: PASS");return 0;
}
