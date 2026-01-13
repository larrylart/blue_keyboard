#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  Turkish layout (TR) for macOS (Turkish Q assumed)
//  Host macOS Input Source must be set to Turkish (Q).
//
//  Goal:
//   - Provide a character->(mods+usage) table for the “non-US” characters,
//     including Turkish letters and common programmer symbols.
//
//  IMPORTANT:
//   - macOS uses Option as the "3rd level" modifier, so we use MOD_ALT.
//   - The physical key positions for Turkish-specific letters differ from US.
//     You MUST set the TR_KEY_* usages below to match your intended TR layout.
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////-
// TR-specific physical key usages (HID Keyboard/Keypad Page)
// Set these to the HID usage ID of the *physical key* that produces the letter
// when macOS is set to Turkish Q input source.
//
// If these are left as 0, the entry is ignored by sendChordOrTap().
////////////////////////////////////////////////////////////////////-
#ifndef TR_KEY_G_BREVE      // ğ / Ğ
  #define TR_KEY_G_BREVE 0x00
#endif
#ifndef TR_KEY_U_UML        // ü / Ü
  #define TR_KEY_U_UML   0x00
#endif
#ifndef TR_KEY_S_CEDILLA    // ş / Ş
  #define TR_KEY_S_CEDILLA 0x00
#endif
#ifndef TR_KEY_DOTLESS_I    // ı / I  (dotless i)
  #define TR_KEY_DOTLESS_I 0x00
#endif
#ifndef TR_KEY_O_UML        // ö / Ö
  #define TR_KEY_O_UML   0x00
#endif
#ifndef TR_KEY_C_CEDILLA    // ç / Ç
  #define TR_KEY_C_CEDILLA 0x00
#endif

// Programmer-symbol physical keys (optional; set if you want explicit control)
// If you don't set these, the characters may still work via normal kb.write()
// *if* your underlying ASCII path matches the host layout.
#ifndef TR_KEY_LBRACKET     // physical key that yields [ or an Alt/Option layer related
  #define TR_KEY_LBRACKET 0x00
#endif
#ifndef TR_KEY_RBRACKET
  #define TR_KEY_RBRACKET 0x00
#endif
#ifndef TR_KEY_BACKSLASH
  #define TR_KEY_BACKSLASH 0x00
#endif
#ifndef TR_KEY_PIPE
  #define TR_KEY_PIPE 0x00
#endif
#ifndef TR_KEY_AT
  #define TR_KEY_AT 0x00
#endif

////////////////////////////////////////////////////////////////////
//  Table
////////////////////////////////////////////////////////////////////
// Each entry: { Unicode codepoint, modifiers, HID usage, modifiers2, usage2 }
// For macOS: Option = MOD_ALT, Shift = MOD_SHIFT.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_TR_MAC[] =
{
  // ------------------------------------------------------------------------
  // Turkish letters (lowercase + uppercase)
  // ------------------------------------------------------------------------
  { 0x011F /*ğ*/,  0x00,      TR_KEY_G_BREVE,    0, 0 },
  { 0x011E /*Ğ*/,  MOD_SHIFT, TR_KEY_G_BREVE,    0, 0 },

  { 0x00FC /*ü*/,  0x00,      TR_KEY_U_UML,      0, 0 },
  { 0x00DC /*Ü*/,  MOD_SHIFT, TR_KEY_U_UML,      0, 0 },

  { 0x015F /*ş*/,  0x00,      TR_KEY_S_CEDILLA,  0, 0 },
  { 0x015E /*Ş*/,  MOD_SHIFT, TR_KEY_S_CEDILLA,  0, 0 },

  { 0x0131 /*ı*/,  0x00,      TR_KEY_DOTLESS_I,  0, 0 },
  { U'I',          MOD_SHIFT, TR_KEY_DOTLESS_I,  0, 0 }, // uppercase dotless is 'I'

  { 0x00F6 /*ö*/,  0x00,      TR_KEY_O_UML,      0, 0 },
  { 0x00D6 /*Ö*/,  MOD_SHIFT, TR_KEY_O_UML,      0, 0 },

  { 0x00E7 /*ç*/,  0x00,      TR_KEY_C_CEDILLA,  0, 0 },
  { 0x00C7 /*Ç*/,  MOD_SHIFT, TR_KEY_C_CEDILLA,  0, 0 },

  // ------------------------------------------------------------------------
  // ISO key left of Z: < >
  // (This one is stable across ISO boards and your common header already
  //  aliases it as ISO_BSLASH_KEY.)
  // ------------------------------------------------------------------------
  { U'<',  0x00,      ISO_BSLASH_KEY, 0, 0 },
  { U'>',  MOD_SHIFT, ISO_BSLASH_KEY, 0, 0 },

  // ------------------------------------------------------------------------
  // Common programmer symbols on macOS
  //
  // NOTE:
  // - € is very commonly Option+E on macOS across many layouts.
  // - The rest (@, \, |, [ ], { }) vary by the exact Turkish input source
  //   ("Turkish", "Turkish-Q PC", etc.). To avoid wrong output, these are
  //   provided as configurable physical-key hooks. Set TR_KEY_* above.
  // ------------------------------------------------------------------------

  { 0x20AC /*€*/, MOD_ALT, 0x08 /*E*/, 0, 0 }, // € = Option+E (common mac behavior)

  // If you want explicit @, set TR_KEY_AT to the *base key* and it will be Option+that key.
  { U'@',  MOD_ALT, TR_KEY_AT, 0, 0 },

  // Brackets / braces: set TR_KEY_LBRACKET / TR_KEY_RBRACKET to the physical base keys.
  { U'[',  MOD_ALT, TR_KEY_LBRACKET, 0, 0 },
  { U']',  MOD_ALT, TR_KEY_RBRACKET, 0, 0 },
  { U'{',  (uint8_t)(MOD_ALT | MOD_SHIFT), TR_KEY_LBRACKET, 0, 0 },
  { U'}',  (uint8_t)(MOD_ALT | MOD_SHIFT), TR_KEY_RBRACKET, 0, 0 },

  // Backslash / pipe: set TR_KEY_BACKSLASH / TR_KEY_PIPE to the physical base keys.
  { U'\\', MOD_ALT, TR_KEY_BACKSLASH, 0, 0 },
  { U'|',  (uint8_t)(MOD_ALT | MOD_SHIFT), TR_KEY_PIPE, 0, 0 },
};

static inline const KbMapEntry* kb_get_TR_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_TR_MAC) / sizeof(KBMAP_TR_MAC[0]));
  return KBMAP_TR_MAC;
}
