#ifndef PS2_GAME_DB_UPDATE_H
#define PS2_GAME_DB_UPDATE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int ps2_game_db_update(const char *target, char *message, size_t message_cap);
#ifdef __cplusplus
}
#endif
#endif
