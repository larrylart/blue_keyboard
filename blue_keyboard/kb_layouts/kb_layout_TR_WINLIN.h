#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Turkish layout (TR) for Windows / Linux (Turkish Q assumed)
//  Host OS must be set to Turkish keyboard layout.
//
//  Includes:
//   - Turkish letters: ğ ü ş ı ö ç (and uppercase Ğ Ü Ş I Ö Ç)
//   - ISO key left of Z: < >
//   - Common programmer symbols via AltGr: @ € { } [ ] \ |
////////////////////////////////////////////////////////////////////
//
// NOTE:
//   Turkish-specific letter keys vary by physical layout.
//   Define TR_KEY_* usages below to match the *physical* key positions
//   on your target keyboard. Once set, the table below becomes complete.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
// TODO: Set these HID usages to the physical keys for Turkish Q
//
// Suggested workflow:
//   - Pick one TR machine.
//   - In a test sketch, print key usage values as you press keys,
//     or temporarily map suspected usages and verify output.
////////////////////////////////////////////////////////////////////
#ifndef TR_KEY_G_BREVE      // ğ / Ğ
  #define TR_KEY_G_BREVE 0x00
#endif
#ifndef TR_KEY_U_UML        // ü / Ü
  #define TR_KEY_U_UML   0x00
#endif
#ifndef TR_KEY_S_CEDILLA    // ş / Ş
  #define TR_KEY_S_CEDILLA 0x00
#endif
#ifndef TR_KEY_DOTLESS_I    // ı / I
  #define TR_KEY_DOTLESS_I 0x00
#endif
#ifndef TR_KEY_O_UML        // ö / Ö
  #define TR_KEY_O_UML   0x00
#endif
#ifndef TR_KEY_C_CEDILLA    // ç / Ç
  #define TR_KEY_C_CEDILLA 0x00
#endif

static const KbMapEntry KBMAP_TR_WINLIN[] =
{
  // ------------------------------------------------------------------------
  // Turkish letters
  // ------------------------------------------------------------------------
  { 0x011F /*ğ*/,  0x00,      TR_KEY_G_BREVE,   0, 0 },
  { 0x011E /*Ğ*/,  MOD_SHIFT, TR_KEY_G_BREVE,   0, 0 },

  { 0x00FC /*ü*/,  0x00,      TR_KEY_U_UML,     0, 0 },
  { 0x00DC /*Ü*/,  MOD_SHIFT, TR_KEY_U_UML,     0, 0 },

  { 0x015F /*ş*/,  0x00,      TR_KEY_S_CEDILLA, 0, 0 },
  { 0x015E /*Ş*/,  MOD_SHIFT, TR_KEY_S_CEDILLA, 0, 0 },

  { 0x0131 /*ı*/,  0x00,      TR_KEY_DOTLESS_I, 0, 0 },
  { U'I',          MOD_SHIFT, TR_KEY_DOTLESS_I, 0, 0 },  // uppercase dotless is 'I'

  { 0x00F6 /*ö*/,  0x00,      TR_KEY_O_UML,     0, 0 },
  { 0x00D6 /*Ö*/,  MOD_SHIFT, TR_KEY_O_UML,     0, 0 },

  { 0x00E7 /*ç*/,  0x00,      TR_KEY_C_CEDILLA, 0, 0 },
  { 0x00C7 /*Ç*/,  MOD_SHIFT, TR_KEY_C_CEDILLA, 0, 0 },

  // ------------------------------------------------------------------------
  // ISO key left of Z: < >
  // ------------------------------------------------------------------------
  { U'<',  0x00,      ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },

  // ------------------------------------------------------------------------
  // Programmer symbols (typical TR Q AltGr mappings)
  // These can vary slightly; adjust if needed once you confirm on target OS.
  // ------------------------------------------------------------------------
  { U'@',  MOD_ALTGR, 0x14 /*Q*/, 0, 0 },          // AltGr+Q (common)
  { U'€',  MOD_ALTGR, 0x08 /*E*/, 0, 0 },          // AltGr+E (common)

  // Common bracket set (often AltGr + number row; verify for your target)
  { U'[',  MOD_ALTGR, 0x2F /*[ key usage*/, 0, 0 }, // placeholder if TR has physical bracket key
  { U']',  MOD_ALTGR, 0x30 /*] key usage*/, 0, 0 }, // placeholder
  { U'{',  MOD_ALTGR | MOD_SHIFT, 0x2F, 0, 0 },
  { U'}',  MOD_ALTGR | MOD_SHIFT, 0x30, 0, 0 },

  // Backslash/pipe often on AltGr+minus / ISO key (verify)
  { U'\\', MOD_ALTGR, 0x2D /*-*/, 0, 0 },
  { U'|',  MOD_ALTGR, ISO_BSLASH_KEY, 0, 0 },
};

static inline const KbMapEntry* kb_get_TR_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_TR_WINLIN) / sizeof(KBMAP_TR_WINLIN[0]));
  return KBMAP_TR_WINLIN;
}
