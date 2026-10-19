#include "ps2iodb_cache.h"
#include <assert.h>
#include <stdio.h>
int main(void){ps2iodb_match m;assert(ps2iodb_lookup("BESLES-50219GAMEDATA",&m)==2&&!m.exact);assert(ps2iodb_lookup("BESLES-50072ARKSFEX3",&m)==2&&!m.exact);assert(ps2iodb_lookup("BASLUS-20187GAMEDATA",&m)==1&&m.exact);assert(ps2iodb_lookup("BESCES-50294GAMEDATA",&m)==0);puts("PS2IODB lookup tests PASS");return 0;}
