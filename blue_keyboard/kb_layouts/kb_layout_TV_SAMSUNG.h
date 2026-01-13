#pragma once
#include "kb_layout_US_WINLIN.h"
#include "kb_layout_common.h"   // for HID_KEY_F8..F12 (if not already visible elsewhere)

// Typing map: reuse US for now
static inline const KbMapEntry* kb_get_TV_SAMSUNG_map(uint16_t& count)
{
    return kb_get_US_WINLIN_map(count);
}

// Samsung doc (Keyboard equivalents to remote control keys):
//   VolumeMute -> F8
//   VolumeDown -> F9
//   VolumeUp   -> F10
//   ChannelDown-> F11
//   ChannelUp  -> F12
static inline TvMediaRemap tv_samsung_remap_consumer(uint8_t u)
{
    switch(u)
    {
        // Standard consumer low bytes (what your Android remote sends):
        // 0xE2 Mute, 0xEA VolDown, 0xE9 VolUp
        case 0xE2: return { true,  (uint8_t)HID_KEY_F8  };  // Mute -> F8
        case 0xEA: return { true,  (uint8_t)HID_KEY_F9  };  // Vol- -> F9
        case 0xE9: return { true,  (uint8_t)HID_KEY_F10 };  // Vol+ -> F10

        // If you later add CH+/CH- in the Android remote and send consumer codes:
        // case 0x9C: return { true, (uint8_t)HID_KEY_F12 }; // ChannelUp
        // case 0x9D: return { true, (uint8_t)HID_KEY_F11 }; // ChannelDown

        default:
            return { false, u }; // passthrough: keep as consumer usage
    }
}