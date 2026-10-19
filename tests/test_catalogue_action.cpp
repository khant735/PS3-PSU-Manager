#include "catalogue_action.h"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
 assert(catalogue_action_for_state(0,0,0)==CATALOGUE_ACTION_CHECK_DOWNLOAD);
 assert(catalogue_action_for_state(1,0,0)==CATALOGUE_ACTION_CHECK_DOWNLOAD);
 assert(catalogue_action_for_state(1,1,0)==CATALOGUE_ACTION_DOWNLOAD);
 assert(catalogue_action_for_state(0,0,1)==CATALOGUE_ACTION_DOWNLOAD);
 assert(catalogue_action_for_state(1,2,0)==CATALOGUE_ACTION_OPEN_FILES);
 assert(std::strcmp(catalogue_action_label(CATALOGUE_ACTION_CHECK_DOWNLOAD),"CHECK / DOWNLOAD")==0);
 assert(std::strcmp(catalogue_action_label(CATALOGUE_ACTION_DOWNLOAD),"DOWNLOAD")==0);
 assert(std::strcmp(catalogue_action_label(CATALOGUE_ACTION_OPEN_FILES),"OPEN FILES")==0);
 puts("PASS: catalogue action labels follow resolved file state");
}
