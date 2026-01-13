#pragma once
#include "kb_layout_US_WINLIN.h"
#include "kb_layout_common.h"   // for TvMediaRemap + HID_KEY_* if needed later

// Typing map: reuse US for now
static inline const KbMapEntry* kb_get_TV_ROKU_map(uint16_t& count)
{
    return kb_get_US_WINLIN_map(count);
}

// Roku documentation describes remote buttons (Play/Pause, Rewind, etc.) but does not
// specify special USB keyboard HID remaps; default passthrough.
static inline TvMediaRemap tv_roku_remap_consumer(uint8_t u)
{
    switch(u)
    {
        default:
            return { false, u }; // passthrough (consumer usage)
    }
}
