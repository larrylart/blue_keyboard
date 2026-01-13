#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  US layout (US) for Windows / Linux (ANSI)
//  Host OS must be set to US layout.
//
//  For US QWERTY, RawKeyboard's default ASCII path already matches the layout,
//  so we don't need any overrides.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_US_WINLIN[] = {
  // empty on purpose
};

static inline const KbMapEntry* kb_get_US_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_US_WINLIN) / sizeof(KBMAP_US_WINLIN[0]));
  return KBMAP_US_WINLIN;
}
