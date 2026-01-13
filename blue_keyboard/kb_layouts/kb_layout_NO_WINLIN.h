#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Norwegian layout (NO) for Windows / Linux (ISO)
//  Host OS must be set to Norwegian keyboard layout.
//
//  Includes:
//   - Norwegian letters: æ ø å (and uppercase Æ Ø Å)
//   - Common programmer symbols via AltGr: @ € { } [ ] \ |
//   - ISO key left of Z: < >
//
//  Notes:
//   - Dead-key composition for accented characters is not included here.
//   - If Æ/Ø/Å land wrong on your setup, tweak the NO_KEY_* constants below.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
// Physical key positions (HID usages) for Norwegian ISO keyboards
//
// On most Nordic layouts:
//   - Å is on the key where US has '['  (usage 0x2F)
//   - Ø is on the key where US has ';'  (usage 0x33)
//   - Æ is on the key where US has quote/apostrophe (usage 0x34 / QUOTE_KEY)
//
// These match the common Nordic physical layout conventions.
////////////////////////////////////////////////////////////////////
#ifndef NO_KEY_A_RING
  #define NO_KEY_A_RING 0x2F   // US '[' position
#endif

#ifndef NO_KEY_O_SLASH
  #define NO_KEY_O_SLASH 0x33  // US ';' position
#endif

#ifndef NO_KEY_AE
  #define NO_KEY_AE QUOTE_KEY  // typically 0x34
#endif

static const KbMapEntry KBMAP_NO_WINLIN[] =
{
  // ------------------------------------------------------------------------
  // Norwegian letters
  // ------------------------------------------------------------------------
  { U'å',  0x00,      NO_KEY_A_RING, 0, 0 },
  { U'Å',  MOD_SHIFT, NO_KEY_A_RING, 0, 0 },

  { U'ø',  0x00,      NO_KEY_O_SLASH, 0, 0 },
  { U'Ø',  MOD_SHIFT, NO_KEY_O_SLASH, 0, 0 },

  { U'æ',  0x00,      NO_KEY_AE, 0, 0 },
  { U'Æ',  MOD_SHIFT, NO_KEY_AE, 0, 0 },

  // ------------------------------------------------------------------------
  // ISO key left of Z: < >
  // ------------------------------------------------------------------------
  { U'<',  0x00,      ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },

  // ------------------------------------------------------------------------
  // Core programmer symbols (Nordic-style AltGr mappings)
  // ------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x1F /*2*/, 0, 0 },              // AltGr+2
  { U'€',  MOD_ALTGR, 0x08 /*E*/, 0, 0 },              // AltGr+E (common Nordic)

  { U'{',  MOD_ALTGR, 0x24 /*7*/, 0, 0 },              // AltGr+7
  { U'[',  MOD_ALTGR, 0x25 /*8*/, 0, 0 },              // AltGr+8
  { U']',  MOD_ALTGR, 0x26 /*9*/, 0, 0 },              // AltGr+9
  { U'}',  MOD_ALTGR, 0x27 /*0*/, 0, 0 },              // AltGr+0

  { U'\\', MOD_ALTGR, 0x2E /*=*/ , 0, 0 },             // AltGr+=
  { U'|',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },          // AltGr+ISO key (common)

  // Optional common: tilde sometimes reachable via AltGr on ISO key (layout-dependent)
  { U'~',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },
};

static inline const KbMapEntry* kb_get_NO_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_NO_WINLIN) / sizeof(KBMAP_NO_WINLIN[0]));
  return KBMAP_NO_WINLIN;
}
