#pragma once
#include "kb_layout_US_WINLIN.h"
#include "kb_layout_common.h"   // for TvMediaRemap + HID_KEY_* if needed later

// Typing map: reuse US for now
static inline const KbMapEntry* kb_get_TV_LG_map(uint16_t& count)
{
    return kb_get_US_WINLIN_map(count);
}

// LG webOS docs publish keycodes for REMOTE control units (not USB HID keyboard mappings),
// so we default to passthrough for standard consumer usages.
//
// If you later discover a specific LG model/app expects keyboard keys (like F-keys)
// for volume/mute, add overrides here.
static inline TvMediaRemap tv_lg_remap_consumer(uint8_t u)
{
    switch(u)
    {
        default:
            return { false, u }; // passthrough (consumer usage)
    }
}
