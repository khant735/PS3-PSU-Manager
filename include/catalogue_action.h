#ifndef CATALOGUE_ACTION_H
#define CATALOGUE_ACTION_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { CATALOGUE_ACTION_CHECK_DOWNLOAD=0, CATALOGUE_ACTION_DOWNLOAD=1, CATALOGUE_ACTION_OPEN_FILES=2 } catalogue_action_t;
catalogue_action_t catalogue_action_for_state(int checked,int file_count,int direct_file);
const char *catalogue_action_label(catalogue_action_t action);
#ifdef __cplusplus
}
#endif
#endif
