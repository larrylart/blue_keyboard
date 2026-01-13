#pragma once
#include "kb_layout_US_WINLIN.h"
#include "kb_layout_common.h"   // for TvMediaRemap + HID_KEY_* if needed later

// Typing map: reuse US for now
static inline const KbMapEntry* kb_get_TV_ANDROID_map(uint16_t& count)
{
    return kb_get_US_WINLIN_map(count);
}

// Android/Google TV: no vendor-wide official "consumer usage -> keyboard key" remap table found.
// Reports suggest media keys work as expected on BT keyboards, so default passthrough. 
static inline TvMediaRemap tv_android_remap_consumer(uint8_t u)
{
    switch(u)
    {
        default:
            return { false, u }; // passthrough (consumer usage)
    }
}
