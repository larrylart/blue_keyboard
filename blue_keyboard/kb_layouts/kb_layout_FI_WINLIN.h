#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Finnish layout (FI) for Windows / Linux (ISO)
//  Host OS must be set to Finnish keyboard layout.
//
//  Includes:
//   - Finnish letters: ä ö (and uppercase Ä Ö)
//   - ISO key left of Z: < >
//   - Common Nordic programmer symbols via AltGr: @ € { } [ ] \ |
//
//  Notes:
//   - Finnish/Swedish physical keyboards share many positions. This mapping
//     assumes the common Nordic ISO physical layout.
//   - Dead-key composition for accented characters is not included here.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
// Physical key positions (HID usages) for Finnish ISO keyboards
//
// On Nordic layouts:
//   - Ä is on the key where US has ']'  (usage 0x30)
//   - Ö is on the key where US has ';'  (usage 0x33)
//
// (Å exists on FI/SV combo keyboards, but Finnish layout typically uses Ä/Ö as primary.)
////////////////////////////////////////////////////////////////////
#ifndef FI_KEY_A_UML
  #define FI_KEY_A_UML 0x30   // US ']' position
#endif

#ifndef FI_KEY_O_UML
  #define FI_KEY_O_UML 0x33   // US ';' position
#endif

static const KbMapEntry KBMAP_FI_WINLIN[] =
{
  // ------------------------------------------------------------------------
  // Finnish letters
  // ------------------------------------------------------------------------
  { U'ä',  0x00,      FI_KEY_A_UML, 0, 0 },
  { U'Ä',  MOD_SHIFT, FI_KEY_A_UML, 0, 0 },

  { U'ö',  0x00,      FI_KEY_O_UML, 0, 0 },
  { U'Ö',  MOD_SHIFT, FI_KEY_O_UML, 0, 0 },

  // ------------------------------------------------------------------------
  // ISO key left of Z: < >
  // ------------------------------------------------------------------------
  { U'<',  0x00,      ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },

  // ------------------------------------------------------------------------
  // Core programmer symbols (Nordic-style AltGr mappings)
  // ------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x1F /*2*/, 0, 0 },              // AltGr+2
  { U'€',  MOD_ALTGR, 0x08 /*E*/, 0, 0 },              // AltGr+E

  { U'{',  MOD_ALTGR, 0x24 /*7*/, 0, 0 },              // AltGr+7
  { U'[',  MOD_ALTGR, 0x25 /*8*/, 0, 0 },              // AltGr+8
  { U']',  MOD_ALTGR, 0x26 /*9*/, 0, 0 },              // AltGr+9
  { U'}',  MOD_ALTGR, 0x27 /*0*/, 0, 0 },              // AltGr+0

  { U'\\', MOD_ALTGR, 0x2E /*=*/ , 0, 0 },             // AltGr+=
  { U'|',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },          // AltGr+ISO key (common)

  // Optional: tilde often on AltGr+ISO key in Nordic variants (layout-dependent)
  { U'~',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },
};

static inline const KbMapEntry* kb_get_FI_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_FI_WINLIN) / sizeof(KBMAP_FI_WINLIN[0]));
  return KBMAP_FI_WINLIN;
}
