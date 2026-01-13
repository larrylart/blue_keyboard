#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Swedish layout (SE) for Windows / Linux (ISO)
//  Host OS must be set to Swedish keyboard layout.
//
//  Includes:
//   - Swedish letters: å ä ö (and uppercase Å Ä Ö)
//   - Common programmer symbols via AltGr: @ € { } [ ] \ |
//   - ISO key left of Z: < > (and AltGr variant for | is common)
//
//  Notes:
//   - Swedish uses dead keys for many accented characters. Not included here.
//   - If any of Å/Ä/Ö land wrong on your setup, tweak the SE_KEY_* constants.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
// Physical key positions (HID usages) for Swedish ISO keyboards
//
// On most Nordic layouts:
//   - Å is on the key where US has '['  (usage 0x2F)
//   - Ä is on the key where US has ']'  (usage 0x30)
//   - Ö is on the key where US has ';'  (usage 0x33)
//
// These are the only layout-specific "letter keys" we need for SE basics.
////////////////////////////////////////////////////////////////////
#ifndef SE_KEY_A_RING
  #define SE_KEY_A_RING 0x2F   // US '[' position
#endif

#ifndef SE_KEY_A_UML
  #define SE_KEY_A_UML  0x30   // US ']' position
#endif

#ifndef SE_KEY_O_UML
  #define SE_KEY_O_UML  0x33   // US ';' position
#endif

static const KbMapEntry KBMAP_SE_WINLIN[] =
{
  // ------------------------------------------------------------------------
  // Swedish letters
  // ------------------------------------------------------------------------
  { U'å',  0x00,      SE_KEY_A_RING, 0, 0 },
  { U'Å',  MOD_SHIFT, SE_KEY_A_RING, 0, 0 },

  { U'ä',  0x00,      SE_KEY_A_UML,  0, 0 },
  { U'Ä',  MOD_SHIFT, SE_KEY_A_UML,  0, 0 },

  { U'ö',  0x00,      SE_KEY_O_UML,  0, 0 },
  { U'Ö',  MOD_SHIFT, SE_KEY_O_UML,  0, 0 },

  // ------------------------------------------------------------------------
  // ISO key left of Z: < >
  // ------------------------------------------------------------------------
  { U'<',  0x00,      ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },

  // ------------------------------------------------------------------------
  // Core programmer symbols (Nordic-style AltGr mappings)
  // These match the logic you used earlier in sendChar_Nordic().
  // ------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x1F /*2*/, 0, 0 },           // AltGr+2
  { U'€',  MOD_ALTGR, 0x08 /*E*/, 0, 0 },           // AltGr+E (common Nordic)

  { U'{',  MOD_ALTGR, 0x24 /*7*/, 0, 0 },           // AltGr+7
  { U'[',  MOD_ALTGR, 0x25 /*8*/, 0, 0 },           // AltGr+8
  { U']',  MOD_ALTGR, 0x26 /*9*/, 0, 0 },           // AltGr+9
  { U'}',  MOD_ALTGR, 0x27 /*0*/, 0, 0 },           // AltGr+0

  { U'\\', MOD_ALTGR, 0x2E /*=*/ , 0, 0 },          // AltGr+=  (Nordic common)
  { U'|',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },       // AltGr+ISO key (very common)

  // ------------------------------------------------------------------------
  // Conservative extras (useful but still fairly stable)
  // ------------------------------------------------------------------------
  { U'~',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },       // often AltGr on ISO key (if supported)
};

// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_SE_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_SE_WINLIN) / sizeof(KBMAP_SE_WINLIN[0]));
  return KBMAP_SE_WINLIN;
}
