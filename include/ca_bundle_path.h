#ifndef PSU_CA_BUNDLE_PATH_H
#define PSU_CA_BUNDLE_PATH_H

#include <stddef.h>

int ps3_ca_bundle_find(const char *const *candidates,size_t candidate_count,
                       char *selected,size_t selected_capacity);

#endif
