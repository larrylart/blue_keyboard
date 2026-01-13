#pragma once
    #include "kb_layout_common.h"
    // Mapping table for CH_FR_WINLIN (generated from current overrides)
static const KbMapEntry KBMAP_CH_FR_WINLIN[] = {
  { '@', MOD_ALTGR, 0x1F, 0, 0 }, // @ = AltGr+2
  { '\\', MOD_ALTGR, 0x2D, 0, 0 }, // \\ = AltGr+-
  { '|', MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 }, // | = AltGr+ISO
};

static inline const KbMapEntry* kb_get_CH_FR_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_CH_FR_WINLIN)/sizeof(KBMAP_CH_FR_WINLIN[0]));
  return KBMAP_CH_FR_WINLIN;
}

