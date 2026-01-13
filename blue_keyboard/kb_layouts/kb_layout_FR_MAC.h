#pragma once
#include "kb_layout_common.h"

////////////////////////////////////////////////////////////////////
//  French layout (FR) for macOS (Apple AZERTY)
//  Host OS must be set to: French (not "French - PC").
//
//  Goal:
//   - Provide a character->(mods+usage[, mods2+usage2]) table for chars that
//     differ from US, including French-specific letters and common coding chars.
////////////////////////////////////////////////////////////////////

// --- Physical key usage aliases (HID Keyboard/Keypad page) ---
// NOTE: These are HID usages for the *physical positions*.
// For AZERTY swaps we map characters explicitly.

// Key left of "1" under Escape (ANSI/Apple): typically hosts @/# on FR Apple keyboards
#define FR_KEY_TLDE  0x35   // Keyboard Grave Accent and Tilde (physical key)

// AZERTY letters that differ from US QWERTY
#define FR_KEY_A_PHYS   0x14 // physical 'Q' key (produces 'A' on AZERTY)
#define FR_KEY_Q_PHYS   0x04 // physical 'A' key (produces 'Q' on AZERTY)
#define FR_KEY_Z_PHYS   0x1A // physical 'W' key (produces 'Z' on AZERTY)
#define FR_KEY_W_PHYS   0x1D // physical 'Z' key (produces 'W' on AZERTY)
#define FR_KEY_M_PHYS   0x33 // physical ';' key (produces 'M' on AZERTY)

// Accented letters on French AZERTY are direct on number row / punctuation keys
// Top row (physical 1..0 usages = 0x1E..0x27):
//   0x1F key = 'é' (Shift gives '2')
//   0x24 key = 'è' (Shift gives '7')
//   0x26 key = 'ç' (Shift gives '9')
//   0x27 key = 'à' (Shift gives '0')
// 'ù' is commonly on the physical QUOTE key position (0x34) on FR Apple AZERTY.
#define FR_KEY_E_ACUTE   0x1F // 'é'
#define FR_KEY_E_GRAVE   0x24 // 'è'
#define FR_KEY_C_CEDIL   0x26 // 'ç'
#define FR_KEY_A_GRAVE   0x27 // 'à'
#define FR_KEY_U_GRAVE   QUOTE_KEY // 0x34 (ù/* on FR Apple)

// Parentheses are on AZERTY top row keys:
// '(' is Shift+5 (usage 0x22), ')' is Shift+° (usage 0x23 on many FR AZERTY)
#define FR_KEY_5         0x22
#define FR_KEY_6         0x23

// Dot key used for backslash on FR mac (commonly Option+Shift+'.' -> '\')
#define FR_KEY_DOT       0x37

////////////////////////////////////////////////////////////////////
//  Table: { codepoint, mods1, key1, mods2, key2 }
//  - For dead-key style outputs you can use mods2/key2 as a 2nd chord.
////////////////////////////////////////////////////////////////////

static const KbMapEntry KBMAP_FR_MAC[] = {

  // --------------------------------------------------------------------------
  // AZERTY letter swaps (so "kb.write('a')" would be wrong on FR; map explicitly)
  // --------------------------------------------------------------------------
  { U'a',  0x00,    FR_KEY_A_PHYS,  0x00, 0x00 },
  { U'A',  MOD_SHIFT, FR_KEY_A_PHYS, 0x00, 0x00 },
  { U'q',  0x00,    FR_KEY_Q_PHYS,  0x00, 0x00 },
  { U'Q',  MOD_SHIFT, FR_KEY_Q_PHYS, 0x00, 0x00 },

  { U'z',  0x00,    FR_KEY_Z_PHYS,  0x00, 0x00 },
  { U'Z',  MOD_SHIFT, FR_KEY_Z_PHYS, 0x00, 0x00 },
  { U'w',  0x00,    FR_KEY_W_PHYS,  0x00, 0x00 },
  { U'W',  MOD_SHIFT, FR_KEY_W_PHYS, 0x00, 0x00 },

  { U'm',  0x00,    FR_KEY_M_PHYS,  0x00, 0x00 },
  { U'M',  MOD_SHIFT, FR_KEY_M_PHYS, 0x00, 0x00 },

  // --------------------------------------------------------------------------
  // Digits on classic AZERTY: require Shift
  // --------------------------------------------------------------------------
  { U'1',  MOD_SHIFT, 0x1E, 0x00, 0x00 },
  { U'2',  MOD_SHIFT, 0x1F, 0x00, 0x00 },
  { U'3',  MOD_SHIFT, 0x20, 0x00, 0x00 },
  { U'4',  MOD_SHIFT, 0x21, 0x00, 0x00 },
  { U'5',  MOD_SHIFT, 0x22, 0x00, 0x00 },
  { U'6',  MOD_SHIFT, 0x23, 0x00, 0x00 },
  { U'7',  MOD_SHIFT, 0x24, 0x00, 0x00 },
  { U'8',  MOD_SHIFT, 0x25, 0x00, 0x00 },
  { U'9',  MOD_SHIFT, 0x26, 0x00, 0x00 },
  { U'0',  MOD_SHIFT, 0x27, 0x00, 0x00 },

  // --------------------------------------------------------------------------
  // French-specific letters (direct keys on FR Apple AZERTY)
  // --------------------------------------------------------------------------
  { U'é',  0x00,     FR_KEY_E_ACUTE,  0x00, 0x00 },
  { U'É',  MOD_SHIFT,FR_KEY_E_ACUTE,  0x00, 0x00 },

  { U'è',  0x00,     FR_KEY_E_GRAVE,  0x00, 0x00 },
  { U'È',  MOD_SHIFT,FR_KEY_E_GRAVE,  0x00, 0x00 },

  { U'à',  0x00,     FR_KEY_A_GRAVE,  0x00, 0x00 },
  { U'À',  MOD_SHIFT,FR_KEY_A_GRAVE,  0x00, 0x00 },

  { U'ç',  0x00,     FR_KEY_C_CEDIL,  0x00, 0x00 },
  { U'Ç',  MOD_SHIFT,FR_KEY_C_CEDIL,  0x00, 0x00 },

  { U'ù',  0x00,     FR_KEY_U_GRAVE,  0x00, 0x00 },
  { U'Ù',  MOD_SHIFT,FR_KEY_U_GRAVE,  0x00, 0x00 },

  // --------------------------------------------------------------------------
  // Coding symbols on macOS French (Apple) AZERTY
  //   { }  = Option + ( / )
  //   [ ]  = Option + Shift + ( / )
  //   |    = Option + Shift + L
  // --------------------------------------------------------------------------
  { U'{',  MOD_ALT,              FR_KEY_5, 0x00, 0x00 },
  { U'}',  MOD_ALT,              FR_KEY_6, 0x00, 0x00 },
  { U'[',  MOD_ALT | MOD_SHIFT,  FR_KEY_5, 0x00, 0x00 },
  { U']',  MOD_ALT | MOD_SHIFT,  FR_KEY_6, 0x00, 0x00 },

  { U'|',  MOD_ALT | MOD_SHIFT,  0x0F /* L */, 0x00, 0x00 },

  // Backslash on FR mac is commonly Option+Shift+'.'
  { U'\\', MOD_ALT | MOD_SHIFT,  FR_KEY_DOT, 0x00, 0x00 },

  // --------------------------------------------------------------------------
  // @ and # (often on the key under Escape on Apple FR keyboards)
  // --------------------------------------------------------------------------
  { U'@',  MOD_ALT,              FR_KEY_TLDE, 0x00, 0x00 },
  { U'#',  MOD_ALT | MOD_SHIFT,  FR_KEY_TLDE, 0x00, 0x00 },

  // --------------------------------------------------------------------------
  // Euro symbol: on macOS French commonly Option+Shift+2
  // (Option held while pressing Shift+2 physical key)
  // --------------------------------------------------------------------------
  { U'€',  MOD_ALT | MOD_SHIFT,  0x1F /* '2' key */, 0x00, 0x00 },

  // --------------------------------------------------------------------------
  // Nice-to-have French ligature œ / Œ (often Option+Q / Option+Shift+Q on macOS)
  // (If your macOS FR input differs, adjust here.)
  // --------------------------------------------------------------------------
  { U'œ',  MOD_ALT,              FR_KEY_A_PHYS /* physical Q */, 0x00, 0x00 },
  { U'Œ',  MOD_ALT | MOD_SHIFT,  FR_KEY_A_PHYS /* physical Q */, 0x00, 0x00 },
};

// Accessor used by your dispatcher
static inline const KbMapEntry* kb_get_FR_MAC_map(uint16_t& count)
{
  count = (uint16_t)(sizeof(KBMAP_FR_MAC) / sizeof(KBMAP_FR_MAC[0]));
  return KBMAP_FR_MAC;
}
