#pragma once
#include "kb_layout_US_WINLIN.h"
#include "kb_layout_common.h"   // for TvMediaRemap + HID_KEY_* if needed later

// Typing map: reuse US for now
static inline const KbMapEntry* kb_get_TV_FIRETV_map(uint16_t& count)
{
    return kb_get_US_WINLIN_map(count);
}

// Fire TV docs focus on remote controllers and app-level remote input constraints; no official
// USB keyboard HID special remap table found. Default passthrough. 
static inline TvMediaRemap tv_firetv_remap_consumer(uint8_t u)
{
    switch(u)
    {
        default:
            return { false, u }; // passthrough (consumer usage)
    }
}
