#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Danish layout (DK) for Windows / Linux (ISO)
//  Host OS must be set to Danish keyboard layout.
//
//  Includes:
//   - Danish letters: æ ø å (and uppercase Æ Ø Å)
//   - ISO key left of Z: < >
//   - Common Nordic programmer symbols via AltGr: @ € { } [ ] \ |
//
//  Notes:
//   - Dead-key composition for accented characters is not included here.
//   - If Æ/Ø/Å land wrong on your setup, tweak the DK_KEY_* constants below.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
// Physical key positions (HID usages) for Danish ISO keyboards
//
// On most Nordic layouts:
//   - Å is on the key where US has '['  (usage 0x2F)
//   - Æ is on the key where US has ']'  (usage 0x30)  (DK differs from NO here)
//   - Ø is on the key where US has ';'  (usage 0x33)
//
// These match common DK ISO positioning.
////////////////////////////////////////////////////////////////////
#ifndef DK_KEY_A_RING
  #define DK_KEY_A_RING 0x2F   // US '[' position
#endif

#ifndef DK_KEY_AE
  #define DK_KEY_AE 0x30       // US ']' position
#endif

#ifndef DK_KEY_O_SLASH
  #define DK_KEY_O_SLASH 0x33  // US ';' position
#endif

static const KbMapEntry KBMAP_DK_WINLIN[] =
{
  // ------------------------------------------------------------------------
  // Danish letters
  // ------------------------------------------------------------------------
  { U'å',  0x00,      DK_KEY_A_RING, 0, 0 },
  { U'Å',  MOD_SHIFT, DK_KEY_A_RING, 0, 0 },

  { U'æ',  0x00,      DK_KEY_AE, 0, 0 },
  { U'Æ',  MOD_SHIFT, DK_KEY_AE, 0, 0 },

  { U'ø',  0x00,      DK_KEY_O_SLASH, 0, 0 },
  { U'Ø',  MOD_SHIFT, DK_KEY_O_SLASH, 0, 0 },

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

static inline const KbMapEntry* kb_get_DK_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_DK_WINLIN) / sizeof(KBMAP_DK_WINLIN[0]));
  return KBMAP_DK_WINLIN;
}
