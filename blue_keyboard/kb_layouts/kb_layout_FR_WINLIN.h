#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  French layout (FR) for Windows / Linux (Legacy AZERTY, ISO)
//  Host OS must be set to French (Legacy, AZERTY) layout.
//
//  This map covers the “US-different” characters:
//   - AZERTY letter swaps (a/q, z/w)
//   - Digits (require Shift)
//   - Punctuation/symbols on the number row
//   - AltGr layer for ~ # { [ | ` \ ^ @ ] }
//   - Common extra keys: ¨ ^, £ $ ¤, ù % µ, < >
//
//  Reference: French (Legacy, AZERTY) key legends. 
////////////////////////////////////////////////////////////////////

// Modifier bits (USB HID keyboard modifier byte)
#ifndef MOD_SHIFT
  #define MOD_SHIFT 0x02
#endif
#ifndef MOD_ALTGR
  // On Win/Linux, AltGr is typically Right-Alt (0x40).
  // Some layouts treat AltGr as Ctrl+Alt internally, but that’s handled at the host.
  #define MOD_ALTGR 0x40
#endif

////////////////////////////////////////////////////////////////////
// Physical key usage aliases (HID Keyboard/Keypad page)
// (These are *physical key positions*, not characters.)
////////////////////////////////////////////////////////////////////

// Digits row physical keys:
#define FR_KEY_1_AMP      0x1E  // base: &   shift: 1
#define FR_KEY_2_EACUTE   0x1F  // base: é   shift: 2   altgr: ~
#define FR_KEY_3_DQUOTE   0x20  // base: "   shift: 3   altgr: #
#define FR_KEY_4_SQUOTE   0x21  // base: '   shift: 4   altgr: {
#define FR_KEY_5_LPAREN   0x22  // base: (   shift: 5   altgr: [
#define FR_KEY_6_MINUS    0x23  // base: -   shift: 6   altgr: |
#define FR_KEY_7_EGRAVE   0x24  // base: è   shift: 7   altgr: `
#define FR_KEY_8_UNDERSC  0x25  // base: _   shift: 8   altgr: \

#define FR_KEY_9_CCED     0x26  // base: ç   shift: 9   altgr: ^
#define FR_KEY_0_AGRAVE   0x27  // base: à   shift: 0   altgr: @

// Keys where US has - and = :
#define FR_KEY_RBRACK     0x2D  // base: )   shift: °   altgr: ]
#define FR_KEY_RBRACE     0x2E  // base: =   shift: +   altgr: }

// Keys where US has [ and ] :
#define FR_KEY_DIAERESIS  0x2F  // base: ¨   shift: ^   (dead keys in OS)
#define FR_KEY_CURRENCY   0x30  // base: $   shift: £   altgr: ¤

// Key where US has ; :
#define FR_KEY_UGRAVE     0x33  // base: ù   shift: %   altgr: µ

// ISO key left of Z (your common header aliases this):
// ISO_BSLASH_KEY (0x64) => base: <  shift: >

// Bottom punctuation cluster (same physical usages as US):
#define FR_KEY_COMMA      0x36  // base: ,   shift: ?
#define FR_KEY_SEMICOLON  0x37  // base: ;   shift: .
#define FR_KEY_COLON      0x38  // base: :   shift: /

////////////////////////////////////////////////////////////////////
// Table: { cp, mods1, key1, mods2, key2 }
// mods2/key2 are 0 if unused.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_FR_WINLIN[] =
{
  // ----- AZERTY letter swaps (type Latin letters correctly when host is FR) -----
  // a <-> q
  { U'a', 0x00, 0x14 /* physical Q */, 0x00, 0x00 },
  { U'A', MOD_SHIFT, 0x14,            0x00, 0x00 },
  { U'q', 0x00, 0x04 /* physical A */, 0x00, 0x00 },
  { U'Q', MOD_SHIFT, 0x04,            0x00, 0x00 },

  // z <-> w
  { U'z', 0x00, 0x1A /* physical W */, 0x00, 0x00 },
  { U'Z', MOD_SHIFT, 0x1A,            0x00, 0x00 },
  { U'w', 0x00, 0x1D /* physical Z */, 0x00, 0x00 },
  { U'W', MOD_SHIFT, 0x1D,            0x00, 0x00 },

  // m is on the key where US has ';' on many FR ISO (physical usage 0x33 still),
  // but letters are usually handled by normal ASCII path; keep this minimal.

  // ----- Digits (require Shift on Legacy AZERTY) -----
  { U'1', MOD_SHIFT, FR_KEY_1_AMP,     0x00, 0x00 },
  { U'2', MOD_SHIFT, FR_KEY_2_EACUTE,  0x00, 0x00 },
  { U'3', MOD_SHIFT, FR_KEY_3_DQUOTE,  0x00, 0x00 },
  { U'4', MOD_SHIFT, FR_KEY_4_SQUOTE,  0x00, 0x00 },
  { U'5', MOD_SHIFT, FR_KEY_5_LPAREN,  0x00, 0x00 },
  { U'6', MOD_SHIFT, FR_KEY_6_MINUS,   0x00, 0x00 },
  { U'7', MOD_SHIFT, FR_KEY_7_EGRAVE,  0x00, 0x00 },
  { U'8', MOD_SHIFT, FR_KEY_8_UNDERSC, 0x00, 0x00 },
  { U'9', MOD_SHIFT, FR_KEY_9_CCED,    0x00, 0x00 },
  { U'0', MOD_SHIFT, FR_KEY_0_AGRAVE,  0x00, 0x00 },

  // ----- Number row base symbols (no shift) -----
  { U'&', 0x00, FR_KEY_1_AMP,          0x00, 0x00 },
  { U'é', 0x00, FR_KEY_2_EACUTE,       0x00, 0x00 },
  { U'"', 0x00, FR_KEY_3_DQUOTE,       0x00, 0x00 },
  { U'\'',0x00, FR_KEY_4_SQUOTE,       0x00, 0x00 },
  { U'(', 0x00, FR_KEY_5_LPAREN,       0x00, 0x00 },
  { U'-', 0x00, FR_KEY_6_MINUS,        0x00, 0x00 },
  { U'è', 0x00, FR_KEY_7_EGRAVE,       0x00, 0x00 },
  { U'_', 0x00, FR_KEY_8_UNDERSC,      0x00, 0x00 },
  { U'ç', 0x00, FR_KEY_9_CCED,         0x00, 0x00 },
  { U'à', 0x00, FR_KEY_0_AGRAVE,       0x00, 0x00 },

  // ----- The two keys after '0' -----
  // key (US '-') : ° ) ]
  { U')', 0x00, FR_KEY_RBRACK,         0x00, 0x00 },
  { U'°', MOD_SHIFT, FR_KEY_RBRACK,    0x00, 0x00 },
  { U']', MOD_ALTGR, FR_KEY_RBRACK,    0x00, 0x00 },

  // key (US '=') : + = }
  { U'=', 0x00, FR_KEY_RBRACE,         0x00, 0x00 },
  { U'+', MOD_SHIFT, FR_KEY_RBRACE,    0x00, 0x00 },
  { U'}', MOD_ALTGR, FR_KEY_RBRACE,    0x00, 0x00 },

  // ----- AltGr layer on number row -----
  { U'~', MOD_ALTGR, FR_KEY_2_EACUTE,  0x00, 0x00 },
  { U'#', MOD_ALTGR, FR_KEY_3_DQUOTE,  0x00, 0x00 },
  { U'{', MOD_ALTGR, FR_KEY_4_SQUOTE,  0x00, 0x00 },
  { U'[', MOD_ALTGR, FR_KEY_5_LPAREN,  0x00, 0x00 },
  { U'|', MOD_ALTGR, FR_KEY_6_MINUS,   0x00, 0x00 },
  { U'`', MOD_ALTGR, FR_KEY_7_EGRAVE,  0x00, 0x00 },
  { U'\\',MOD_ALTGR, FR_KEY_8_UNDERSC, 0x00, 0x00 },
  { U'^', MOD_ALTGR, FR_KEY_9_CCED,    0x00, 0x00 },
  { U'@', MOD_ALTGR, FR_KEY_0_AGRAVE,  0x00, 0x00 },

  // ----- Euro -----
  // On FR Legacy, € is AltGr+E. 
  { U'€', MOD_ALTGR, 0x08 /* E */,     0x00, 0x00 },

  // ----- ¨ and ^ key (often dead keys; still useful) -----
  { U'¨', 0x00,      FR_KEY_DIAERESIS, 0x00, 0x00 },
  { U'^', MOD_SHIFT, FR_KEY_DIAERESIS, 0x00, 0x00 },

  // ----- Currency key: $ £ ¤ -----
  { U'$', 0x00,      FR_KEY_CURRENCY,  0x00, 0x00 },
  { U'£', MOD_SHIFT, FR_KEY_CURRENCY,  0x00, 0x00 },
  { U'¤', MOD_ALTGR, FR_KEY_CURRENCY,  0x00, 0x00 },

  // ----- ù % µ -----
  { U'ù', 0x00,      FR_KEY_UGRAVE,    0x00, 0x00 },
  { U'%', MOD_SHIFT, FR_KEY_UGRAVE,    0x00, 0x00 },
  { U'µ', MOD_ALTGR, FR_KEY_UGRAVE,    0x00, 0x00 },

  // ----- ISO key left of Z: < > -----
  { U'<', 0x00,      ISO_BSLASH_KEY,   0x00, 0x00 },
  { U'>', MOD_SHIFT, ISO_BSLASH_KEY,   0x00, 0x00 },

  // ----- Bottom punctuation cluster (FR differs) -----
  { U',', 0x00,      FR_KEY_COMMA,     0x00, 0x00 },
  { U'?', MOD_SHIFT, FR_KEY_COMMA,     0x00, 0x00 },

  { U';', 0x00,      FR_KEY_SEMICOLON, 0x00, 0x00 },
  { U'.', MOD_SHIFT, FR_KEY_SEMICOLON, 0x00, 0x00 },

  { U':', 0x00,      FR_KEY_COLON,     0x00, 0x00 },
  { U'/', MOD_SHIFT, FR_KEY_COLON,     0x00, 0x00 },
};


// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_FR_WINLIN_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_FR_WINLIN) / sizeof(KBMAP_FR_WINLIN[0]));
  return KBMAP_FR_WINLIN;
}
