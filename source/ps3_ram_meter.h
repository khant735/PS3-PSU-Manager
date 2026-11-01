#ifndef PS3_RAM_METER_H
#define PS3_RAM_METER_H
#ifdef __cplusplus
extern "C" {
#endif
/* LV2 user-memory pool totals, not whole-console physical RAM. */
int ps3_ram_get_user_pool(unsigned int *total_bytes,unsigned int *available_bytes);
#ifdef __cplusplus
}
#endif
#endif
