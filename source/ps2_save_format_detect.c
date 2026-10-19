#include "ps2_save_format_detect.h"
#include <string.h>
ps2_save_format ps2_save_detect_header(const unsigned char *data, size_t size) {
    if (!data) return PS2_SAVE_UNKNOWN;
    if (size >= 12 && memcmp(data, "Ps2PowerSave", 12) == 0) return PS2_SAVE_MAX;
    if (size >= 4 && memcmp(data, "CFU\0", 4) == 0) return PS2_SAVE_CBS;
    /* Apollo import_ps2.c expects SharkPortSave signature at byte offset 4. */
    if (size >= 20 && memcmp(data + 4, "SharkPortSave\0\0\0", 16) == 0) return PS2_SAVE_XPS_SPS;
    return PS2_SAVE_UNKNOWN;
}
const char *ps2_save_format_label(ps2_save_format fmt) {
    switch (fmt) {
        case PS2_SAVE_MAX: return "Action Replay MAX (candidate)";
        case PS2_SAVE_CBS: return "CodeBreaker (candidate)";
        case PS2_SAVE_XPS_SPS: return "SharkPort/X-Port (candidate)";
        default: return "Unknown / not identified";
    }
}
