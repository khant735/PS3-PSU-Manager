#include "catalogue_action.h"

catalogue_action_t catalogue_action_for_state(int checked,int file_count,int direct_file){
 if(file_count>1)return CATALOGUE_ACTION_OPEN_FILES;
 if(file_count==1||direct_file)return CATALOGUE_ACTION_DOWNLOAD;
 (void)checked;
 return CATALOGUE_ACTION_CHECK_DOWNLOAD;
}

const char *catalogue_action_label(catalogue_action_t action){
 switch(action){
  case CATALOGUE_ACTION_DOWNLOAD:return "DOWNLOAD";
  case CATALOGUE_ACTION_OPEN_FILES:return "OPEN FILES";
  default:return "CHECK / DOWNLOAD";
 }
}
